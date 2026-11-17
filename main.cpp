#include "Manipulate.h"

#include <cstdlib>
#include <iostream>

namespace {

const char* stateName(GameState s) {
    switch (s) {
    case GameState::NightIntro:  return "NightIntro";
    case GameState::Playing:     return "Playing";
    case GameState::PowerOutage: return "PowerOutage";
    case GameState::Jumpscare:   return "Jumpscare";
    case GameState::GameOver:    return "GameOver";
    case GameState::Victory:     return "Victory";
    }
    return "?";
}

const char* eventName(GameEventType t) {
    switch (t) {
    case GameEventType::HourChanged:        return "HourChanged";
    case GameEventType::PowerOut:           return "PowerOut";
    case GameEventType::JumpscareTriggered: return "JumpscareTriggered";
    case GameEventType::Victory:            return "Victory";
    case GameEventType::AnimatronicMoved:   return "AnimatronicMoved";
    case GameEventType::DoorSound:          return "DoorSound";
    case GameEventType::CameraNoise:        return "CameraNoise";
    case GameEventType::Noise:              return "Noise";
    case GameEventType::DeepSeekHumming:    return "DeepSeekHumming";
    case GameEventType::DeepSeekCrawling:   return "DeepSeekCrawling";
    case GameEventType::DeepSeekNeutralized:return "DeepSeekNeutralized";
    }
    return "?";
}

const char* routeTag(Route r) {
    switch (r) {
    case Route::LeftDoor:  return "L";
    case Route::RightDoor: return "R";
    case Route::Vent:      return "V";
    }
    return "?";
}

const char* doorState(bool armed, bool closed) {
    if (armed) return "ARMED";
    if (closed) return "arming";
    return "open";
}

} // namespace

int main(int argc, char** argv) {
    int night = 5;
    if (argc > 1) night = std::atoi(argv[1]);
    if (night < 1) night = 1;
    if (night > 5) night = 5;

    GameWorld world;
    world.startNight(night);
    std::cout << "=== Night " << night << " ("
              << world.snapshot().animatronics.size() << " animatronics) ===\n";

    bool basinDone = false;

    // 演示：每 1 秒推进一帧
    for (int t = 0; t < 400; ++t) {
        if (t == 2) { world.handleCommand({CommandType::ToggleLeftDoor});
                      world.handleCommand({CommandType::ToggleRightDoor}); }
        if (t == 4) { world.handleCommand({CommandType::ToggleLeftDetect});
                      world.handleCommand({CommandType::ToggleRightDetect}); }
        if (t == 8) world.handleCommand({CommandType::OpenCamera});

        world.update(1.0f);

        GameSnapshot snap = world.snapshot();

        // DeepSeek 开始哼歌 -> 在监控里找到它，带上铁盆
        if (snap.deepSeekHumming && !basinDone) {
            int dsRoom = -1;
            for (const AnimatronicState& a : snap.animatronics)
                if (a.type == AnimatronicType::DeepSeek) dsRoom = a.room;
            if (dsRoom >= 0) {
                world.handleCommand({CommandType::SelectCamera, dsRoom});
                world.handleCommand({CommandType::ApplyBasin});
                basinDone = true;
            }
        }

        if (t % 10 == 0) {
            std::cout << "t=" << t << "s  " << stateName(snap.state)
                      << "  hour=" << snap.hour
                      << "  power=" << static_cast<int>(snap.power)
                      << "  L=" << doorState(snap.leftDoorArmed, snap.leftDoorClosed)
                      << "  R=" << doorState(snap.rightDoorArmed, snap.rightDoorClosed)
                      << "  det=" << (snap.leftDetectOn ? "L" : "") << (snap.rightDetectOn ? "R" : "")
                      << "  cam=" << snap.currentCamera;
            if (snap.leftDoorAI)  std::cout << "  [LEFT DOOR AI!]";
            if (snap.rightDoorAI) std::cout << "  [RIGHT DOOR AI!]";
            if (snap.deepSeekHumming) std::cout << "  [DeepSeek HUMMING]";
            if (snap.deepSeekCrawling) std::cout << "  [DeepSeek CRAWLING]";
            std::cout << "  anims:";
            for (const AnimatronicState& a : snap.animatronics) {
                std::cout << " " << animatronicName(a.type) << "@" << routeTag(a.route)
                          << a.room << (a.atDoor ? "!" : "");
            }
            std::cout << "\n";
        }

        for (const GameEvent& e : world.drainEvents()) {
            std::cout << "  [event] " << eventName(e.type);
            if (e.animatronicId >= 0)
                std::cout << " " << animatronicName(static_cast<AnimatronicType>(e.animatronicId));
            if (e.room >= 0) std::cout << " room=" << e.room;
            std::cout << "\n";
        }

        if (snap.state == GameState::GameOver || snap.state == GameState::Victory)
            break;
    }

    GameSnapshot snap = world.snapshot();
    std::cout << "\nEnd: " << stateName(snap.state)
              << "  hour=" << snap.hour
              << "  power=" << static_cast<int>(snap.power) << "\n";
    return 0;
}
