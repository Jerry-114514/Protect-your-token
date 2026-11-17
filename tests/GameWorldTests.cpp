#include "Manipulate.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type, int id = -1) {
    for (const auto& event : events) {
        if (event.type == type && (id < 0 || event.animatronicId == id)) return true;
    }
    return false;
}

void testClockAndFrameSize() {
    GameWorld whole;
    GameWorld frames;
    for (GameWorld* world : {&whole, &frames}) {
        world->startNight(1, 42);
        world->handleCommand({CommandType::OpenCamera});
        world->handleCommand({CommandType::SelectCamera, 0});
        world->update(-1.f);
        world->update(std::numeric_limits<float>::infinity());
        world->update(std::numeric_limits<float>::quiet_NaN());
        require(world->snapshot().power == 100.f, "Invalid time changed the world");
    }
    whole.update(360.f);
    for (int frame = 0; frame < 360 * 64; ++frame) frames.update(1.f / 64.f);
    for (GameWorld* world : {&whole, &frames}) {
        const auto snapshot = world->snapshot();
        require(snapshot.state == GameState::Victory && snapshot.hour == 6, "Missing dawn victory");
        require(snapshot.animatronics.front().room == 0, "Watched GPT moved");
        require(std::abs(snapshot.power - 10.f) < 0.001f, "Incorrect total power drain");
        int nextHour = 1;
        int victories = 0;
        for (const auto& event : world->drainEvents()) {
            if (event.type == GameEventType::HourChanged)
                require(event.room == nextHour++, "Lost or reordered hour event");
            if (event.type == GameEventType::Victory) ++victories;
        }
        require(nextHour == 7 && victories == 1, "Incomplete dawn events");
    }
}

void testDoorAndOutage() {
    GameWorld world;
    world.startNight(1, 42);
    world.handleCommand({CommandType::ToggleRightDetect});
    for (int tick = 0; tick < 150 * 32 && !world.snapshot().rightDoorAI; ++tick)
        world.update(1.f / 32.f);
    require(world.snapshot().rightDoorAI, "Detector never saw GPT");
    world.update(1.f); // One second of player reaction time
    world.handleCommand({CommandType::ToggleRightDoor});
    world.update(2.96875f);
    require(!world.snapshot().rightDoorArmed, "Door armed before 3 seconds");
    world.update(0.03125f);
    require(world.snapshot().rightDoorArmed, "Door failed to arm at 3 seconds");
    world.update(3.f);
    require(world.snapshot().state == GameState::Playing && !world.snapshot().rightDoorAI,
            "Armed door failed to repel GPT");

    world.startNight(1, 42);
    world.handleCommand({CommandType::ToggleLeftDoor});
    world.handleCommand({CommandType::ToggleRightDoor});
    world.handleCommand({CommandType::ToggleLeftDetect});
    world.handleCommand({CommandType::ToggleRightDetect});
    world.handleCommand({CommandType::OpenCamera});
    world.handleCommand({CommandType::SelectCamera, 0});
    world.update(233.f);
    const auto outage = world.snapshot();
    require(outage.state == GameState::PowerOutage && outage.power == 0.f, "Expected outage");
    require(!outage.leftDoorClosed && !outage.rightDoorClosed && !outage.leftDoorArmed &&
            !outage.rightDoorArmed && !outage.leftDetectOn && !outage.rightDetectOn &&
            !outage.cameraOpen, "Outage left defenses active");
    require(hasEvent(world.drainEvents(), GameEventType::PowerOut), "Missing outage event");
    world.handleCommand({CommandType::OpenCamera});
    require(!world.snapshot().cameraOpen, "Unpowered camera reopened");
    world.update(8.f);
    require(world.snapshot().state == GameState::PowerOutage, "Outage caused automatic defeat");
    world.update(180.f);
    require(world.snapshot().state == GameState::GameOver, "GPT did not attack after outage");
    require(hasEvent(world.drainEvents(), GameEventType::JumpscareTriggered, 0), "Missing GPT attack");

    world.startNight(1, 42);
    world.handleCommand({CommandType::OpenCamera});
    world.handleCommand({CommandType::SelectCamera, 0});
    world.update(287.f);
    world.handleCommand({CommandType::ToggleLeftDoor});
    world.handleCommand({CommandType::ToggleRightDoor});
    world.update(73.f);
    require(world.snapshot().state == GameState::Victory && world.snapshot().power == 0.f,
            "Outage near dawn prevented victory without an intrusion");
}

void testDeepSeek() {
    GameWorld world;
    world.startNight(5, 42);
    world.handleCommand({CommandType::ToggleLeftDoor});
    world.handleCommand({CommandType::ToggleRightDoor});
    world.update(15.f);
    require(world.snapshot().deepSeekHumming, "DeepSeek did not start humming at 15 seconds");
    const auto events = world.drainEvents();
    require(hasEvent(events, GameEventType::DeepSeekHumming, 4), "Missing humming event");
    for (const auto& event : events) {
        if (event.type == GameEventType::DeepSeekHumming)
            require(event.room == -1, "Humming revealed the hiding room");
    }
    const int room = world.snapshot().animatronics.back().room;
    world.handleCommand({CommandType::SelectCamera, room});
    world.handleCommand({CommandType::ApplyBasin});
    require(world.snapshot().deepSeekHumming, "Basin applied with the camera closed");
    world.handleCommand({CommandType::OpenCamera});
    world.handleCommand({CommandType::SelectCamera, 0});
    world.handleCommand({CommandType::ApplyBasin});
    require(world.snapshot().deepSeekHumming, "Basin applied from the wrong camera");
    world.handleCommand({CommandType::SelectCamera, room});
    world.handleCommand({CommandType::ApplyBasin});
    require(world.snapshot().deepSeekNeutralized, "Basin failed on the correct camera");
    require(hasEvent(world.drainEvents(), GameEventType::DeepSeekNeutralized, 4),
            "Basin event was not available immediately");

    for (const auto seed : {42u, 99u}) {
        world.startNight(5, seed);
        world.handleCommand({CommandType::ToggleLeftDoor});
        world.handleCommand({CommandType::ToggleRightDoor});
        world.update(27.f);
        require(world.snapshot().deepSeekCrawling, "DeepSeek missed the humming deadline");
        world.update(10.46875f);
        require(world.snapshot().state == GameState::Playing, "Vent attack arrived early");
        world.update(0.03125f);
        require(hasEvent(world.drainEvents(), GameEventType::JumpscareTriggered, 4),
                "DeepSeek was blocked by a door or did not arrive on time");
    }
}

void testNightFiveDefense() {
    GameWorld world;
    world.startNight(5, 42);
    world.handleCommand({CommandType::ToggleLeftDetect});
    world.handleCommand({CommandType::ToggleRightDetect});

    for (int second = 0; second < 360; ++second) {
        const auto snapshot = world.snapshot();
        if (snapshot.leftDoorAI != snapshot.leftDoorClosed)
            world.handleCommand({CommandType::ToggleLeftDoor});
        if (snapshot.rightDoorAI != snapshot.rightDoorClosed)
            world.handleCommand({CommandType::ToggleRightDoor});
        if (snapshot.deepSeekHumming) {
            world.handleCommand({CommandType::OpenCamera});
            for (int room = 1; room < CameraSystem::kCameraCount - 1; ++room) {
                world.handleCommand({CommandType::SelectCamera, room});
                world.handleCommand({CommandType::ApplyBasin});
                if (world.snapshot().deepSeekNeutralized) break;
            }
            world.handleCommand({CommandType::CloseCamera});
        }
        world.update(1.f);
    }
    const auto snapshot = world.snapshot();
    std::cout << "Night 5 seed 42: hour " << snapshot.hour << ", state "
              << static_cast<int>(snapshot.state) << ", " << snapshot.power << " token remaining\n";
    require(snapshot.state == GameState::Victory && snapshot.power > 0.f,
            "Timed use of both doors did not reach dawn with power");
    require(hasEvent(world.drainEvents(), GameEventType::Noise, 3), "DouBao never emitted a sound");
}
} // namespace

int main() {
    try {
        testClockAndFrameSize();
        testDoorAndOutage();
        testDeepSeek();
        testNightFiveDefense();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
