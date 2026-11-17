#include "Manipulate.h"

#include <algorithm>
#include <cmath>

namespace {
struct CharacterDef {
    AnimatronicType type;
    Route route;
    bool freezesWhenWatched; // GPT：被监控盯着就不动
    float moveIntervalMin;
    float moveIntervalMax;
};

// 固定阵容：夜 1 先出 GPT，之后每夜按顺序多登场一个
const CharacterDef kRoster[AnimatronicAI::kMaxAnimatronics] = {
    {AnimatronicType::GPT,      Route::RightDoor, true,  4.0f, 6.0f}, // 被盯着就不动
    {AnimatronicType::Claude,   Route::LeftDoor,  false, 4.0f, 6.0f},
    {AnimatronicType::QWen,     Route::RightDoor, false, 4.0f, 6.0f},
    {AnimatronicType::DouBao,   Route::LeftDoor,  false, 1.5f, 2.5f}, // 移动极快
    {AnimatronicType::DeepSeek, Route::Vent,      false, 0.0f, 0.0f}, // 特殊机制，不走通用逻辑
};
} // namespace

const char* animatronicName(AnimatronicType type) {
    switch (type) {
    case AnimatronicType::GPT:      return "GPT 6 Astra";
    case AnimatronicType::Claude:   return "Claude Fable 5.2";
    case AnimatronicType::QWen:     return "QWen 4 Max";
    case AnimatronicType::DouBao:   return "DouBao";
    case AnimatronicType::DeepSeek: return "DeepSeek V4.1 Flash";
    }
    return "?";
}

// ---------------- PowerSystem ----------------

void PowerSystem::reset() {
    power_ = 100.f;
}

void PowerSystem::update(float dt,
                         bool leftDoorClosed, bool rightDoorClosed,
                         bool leftDetectOn, bool rightDetectOn,
                         bool cameraOpen) {
    double drain = kBaseDrain;
    if (leftDoorClosed)  drain += kDoorDrain;
    if (rightDoorClosed) drain += kDoorDrain;
    if (leftDetectOn)    drain += kDetectDrain;
    if (rightDetectOn)   drain += kDetectDrain;
    if (cameraOpen)      drain += kCameraDrain;

    const double remaining = power_ - drain * dt;
    power_ = remaining > 1e-9 ? remaining : 0.0;
}

// ---------------- AnimatronicAI ----------------

void AnimatronicAI::reset(int night) {
    states_.clear();
    moveTimers_.clear();
    retreatTimers_.clear();
    attackTimers_.clear();
    movedIds_.clear();
    reachedPlayer_ = false;
    intruderId_ = -1;

    // 登场数量 = 夜数，封顶 5 个；按固定阵容顺序逐个登场
    int count = std::max(1, std::min(night, kMaxAnimatronics));
    for (int i = 0; i < count; ++i) {
        AnimatronicState a;
        a.id = i;
        a.type = kRoster[i].type;
        a.route = kRoster[i].route;
        a.room = 0;
        states_.push_back(a);
        moveTimers_.push_back(kRoster[i].moveIntervalMin);
        retreatTimers_.push_back(0.f);
        attackTimers_.push_back(0.f);
    }

    // 夜数越高越凶
    aggression_ = std::min(0.9f, 0.3f + night * 0.1f);

    // DeepSeek 状态复位
    deepSeekPhase_ = DeepSeekPhase::Dormant;
    deepSeekHumTimer_ = 0.f;
    deepSeekCrawlTimer_ = 0.f;
    deepSeekVentSteps_ = 0;
}

bool AnimatronicAI::isAtDoor(Route route) const {
    for (const AnimatronicState& s : states_) {
        if (s.route == route && s.atDoor) return true;
    }
    return false;
}

bool AnimatronicAI::isDeepSeekHumming() const { return deepSeekPhase_ == DeepSeekPhase::Humming; }
bool AnimatronicAI::isDeepSeekCrawling() const { return deepSeekPhase_ == DeepSeekPhase::Crawling; }
bool AnimatronicAI::isDeepSeekNeutralized() const { return deepSeekPhase_ == DeepSeekPhase::Neutralized; }

int AnimatronicAI::deepSeekRoom() const {
    for (const AnimatronicState& s : states_) {
        if (s.type == AnimatronicType::DeepSeek) return s.room;
    }
    return -1;
}

bool AnimatronicAI::applyBasin(int watchedRoom) {
    // 只有在哼歌阶段、且摄像头正对着 DeepSeek 藏身房间时才能带上铁盆
    if (deepSeekPhase_ != DeepSeekPhase::Humming) return false;
    if (watchedRoom != deepSeekRoom()) return false;
    deepSeekPhase_ = DeepSeekPhase::Neutralized;
    return true;
}

void AnimatronicAI::updateDeepSeek(float dt, float nightElapsed) {
    AnimatronicState* ds = nullptr;
    for (AnimatronicState& s : states_) {
        if (s.type == AnimatronicType::DeepSeek) { ds = &s; break; }
    }
    if (!ds) return; // DeepSeek 尚未登场（night < 5）

    switch (deepSeekPhase_) {
    case DeepSeekPhase::Dormant:
        if (nightElapsed >= kHummingDelay) {
            deepSeekPhase_ = DeepSeekPhase::Humming;
            deepSeekHumTimer_ = kHummingDuration;
            std::uniform_int_distribution<int> roomPick(1, kPathLength - 2);
            ds->room = roomPick(rng_); // 藏到某个房间哼歌
        }
        break;

    case DeepSeekPhase::Humming:
        deepSeekHumTimer_ -= dt;
        if (deepSeekHumTimer_ <= 0.f) {
            // 没处理掉：开始爬管道进攻，不可拦截
            deepSeekPhase_ = DeepSeekPhase::Crawling;
            deepSeekCrawlTimer_ = 0.f;
            ds->atDoor = false;
            ds->room = 0;
        }
        break;

    case DeepSeekPhase::Crawling:
        deepSeekCrawlTimer_ += dt;
        if (deepSeekCrawlTimer_ >= kDeepSeekCrawlInterval) {
            deepSeekCrawlTimer_ -= kDeepSeekCrawlInterval;
            ++deepSeekVentSteps_;
            ds->room = deepSeekVentSteps_;
            movedIds_.push_back(ds->id);
            if (deepSeekVentSteps_ == kDeepSeekVentSteps) {
                ds->room = kPathLength - 1;
                reachedPlayer_ = true;
                intruderId_ = ds->id; // 不可拦截，直接冲进来
            }
        }
        break;

    case DeepSeekPhase::Neutralized:
        // 已带铁盆，本夜不再行动
        break;
    }
}

void AnimatronicAI::update(float dt, float nightElapsed, int watchedRoom,
                           bool leftDoorArmed, bool rightDoorArmed) {
    movedIds_.clear();

    // 1. DeepSeek 特殊状态机
    updateDeepSeek(dt, nightElapsed);
    if (reachedPlayer_) return;

    // 2. 其余 4 个玩偶的通用路线逻辑
    for (std::size_t i = 0; i < states_.size(); ++i) {
        AnimatronicState& s = states_[i];
        if (s.type == AnimatronicType::DeepSeek) continue;

        const CharacterDef& def = kRoster[s.id];
        float& moveTimer = moveTimers_[i];
        float& retreat = retreatTimers_[i];
        float& attack = attackTimers_[i];

        bool blocked = (s.route == Route::LeftDoor) ? leftDoorArmed : rightDoorArmed;

        // 到入口后先等待。连续拦截三秒使其退回。
        if (s.atDoor) {
            attack -= dt;
            if (!blocked) {
                retreat = kRetreatDelay;
                if (attack <= 0.f) {
                    reachedPlayer_ = true;
                    intruderId_ = s.id;
                    return;
                }
                continue;
            }
            retreat -= dt;
            if (retreat <= 0.f) {
                s.room = kPathLength - 3;
                s.atDoor = false;
                moveTimer = def.moveIntervalMin;
                movedIds_.push_back(s.id); // 退回也算一次移动
            }
            continue;
        }

        if (def.freezesWhenWatched && watchedRoom == s.room) continue;
        moveTimer -= dt;
        if (moveTimer > 0.f) continue;

        std::uniform_real_distribution<float> interval(def.moveIntervalMin, def.moveIntervalMax);
        moveTimer += interval(rng_);

        std::bernoulli_distribution roll(aggression_);
        if (roll(rng_)) {
            ++s.room;
            movedIds_.push_back(s.id);
            if (s.room >= kPathLength - 1) {
                s.room = kPathLength - 1;
                s.atDoor = true;
                retreat = kRetreatDelay;
                attack = kAttackDelay;
            }
        }
    }
}

// ---------------- CameraSystem ----------------

void CameraSystem::reset() {
    open_ = false;
    current_ = -1;
}

void CameraSystem::select(int cameraId) {
    if (cameraId >= 0 && cameraId < kCameraCount) {
        current_ = cameraId;
    }
}

// ---------------- GameWorld ----------------

void GameWorld::startNight(int night) {
    night_ = std::max(1, std::min(night, 5));
    state_ = GameState::Playing;
    nightElapsed_ = 0.f;
    pendingTime_ = 0.0;
    currentHour_ = 0;
    leftDoorClosed_ = false;
    rightDoorClosed_ = false;
    leftArmTimer_ = 0.f;
    rightArmTimer_ = 0.f;
    leftDetectOn_ = false;
    rightDetectOn_ = false;
    jumpscareTimer_ = 0.f;
    dsWasHumming_ = false;
    dsWasCrawling_ = false;
    power_.reset();
    ai_.reset(night_);
    cameras_.reset();
    events_.clear();
}

void GameWorld::startNight(int night, std::uint32_t seed) {
    ai_.seed(seed);
    startNight(night);
}

void GameWorld::emit(GameEventType type, int animatronicId, int room) {
    GameEvent e;
    e.type = type;
    e.animatronicId = animatronicId;
    e.room = room;
    events_.push_back(e);
}

void GameWorld::triggerJumpscare(int animatronicId) {
    state_ = GameState::Jumpscare;
    jumpscareTimer_ = kJumpscareDuration;
    emit(GameEventType::JumpscareTriggered, animatronicId);
}

void GameWorld::handleCommand(const Command& cmd) {
    // 只有 Playing 状态响应玩家操作
    if (state_ != GameState::Playing) return;

    switch (cmd.type) {
    case CommandType::ToggleLeftDoor:
        leftDoorClosed_ = !leftDoorClosed_;
        if (!leftDoorClosed_) leftArmTimer_ = 0.f; // 重开要重新等 3 秒
        emit(GameEventType::DoorSound);
        break;
    case CommandType::ToggleRightDoor:
        rightDoorClosed_ = !rightDoorClosed_;
        if (!rightDoorClosed_) rightArmTimer_ = 0.f;
        emit(GameEventType::DoorSound);
        break;
    case CommandType::ToggleLeftDetect:
        leftDetectOn_ = !leftDetectOn_;
        break;
    case CommandType::ToggleRightDetect:
        rightDetectOn_ = !rightDetectOn_;
        break;
    case CommandType::OpenCamera:
        cameras_.open();
        emit(GameEventType::CameraNoise);
        break;
    case CommandType::CloseCamera:
        cameras_.close();
        break;
    case CommandType::SelectCamera:
        cameras_.select(cmd.cameraId);
        break;
    case CommandType::ApplyBasin:
        if (ai_.applyBasin(cameras_.isOpen() ? cameras_.current() : -1))
            emit(GameEventType::DeepSeekNeutralized, static_cast<int>(AnimatronicType::DeepSeek));
        break;
    }
}

void GameWorld::disableDefenses() {
    leftDoorClosed_ = false;
    rightDoorClosed_ = false;
    leftArmTimer_ = 0.f;
    rightArmTimer_ = 0.f;
    leftDetectOn_ = false;
    rightDetectOn_ = false;
    cameras_.close();
}

void GameWorld::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.f) return;

    pendingTime_ += dt;
    while (pendingTime_ >= kStep) {
        if (state_ == GameState::NightIntro || state_ == GameState::GameOver ||
            state_ == GameState::Victory) {
            pendingTime_ = 0.0;
            return;
        }
        pendingTime_ -= kStep;
        advanceStep();
    }
}

void GameWorld::advanceStep() {
    constexpr float dt = kStep;
    if (state_ == GameState::Playing || state_ == GameState::PowerOutage) {
        nightElapsed_ += dt;

        if (state_ == GameState::Playing) {
            if (leftDoorClosed_) leftArmTimer_ += dt;
            if (rightDoorClosed_) rightArmTimer_ += dt;
            power_.update(dt, leftDoorClosed_, rightDoorClosed_,
                          leftDetectOn_, rightDetectOn_, cameras_.isOpen());
            if (power_.isOut()) {
                disableDefenses();
                state_ = GameState::PowerOutage;
                emit(GameEventType::PowerOut);
            }
        }

        const int newHour = static_cast<int>(nightElapsed_ / kHourDuration);
        if (newHour > currentHour_) {
            currentHour_ = newHour;
            emit(GameEventType::HourChanged, -1, currentHour_);
        }
        if (currentHour_ == 6) {
            state_ = GameState::Victory;
            emit(GameEventType::Victory);
            return;
        }

        const int watchedRoom = cameras_.isOpen() ? cameras_.current() : -1;
        ai_.update(dt, nightElapsed_, watchedRoom,
                   leftArmTimer_ >= kInterceptArmTime, rightArmTimer_ >= kInterceptArmTime);

        // 移动事件（DouBao 移动附带声响）
        for (int id : ai_.movedIds()) {
            const AnimatronicState& actor = ai_.states()[id];
            emit(GameEventType::AnimatronicMoved, id, actor.room);
            if (actor.type == AnimatronicType::DouBao) emit(GameEventType::Noise, id);
        }

        // DeepSeek 阶段变化事件
        int dsId = static_cast<int>(AnimatronicType::DeepSeek);
        if (ai_.isDeepSeekHumming() && !dsWasHumming_)
            emit(GameEventType::DeepSeekHumming, dsId);
        if (ai_.isDeepSeekCrawling() && !dsWasCrawling_)
            emit(GameEventType::DeepSeekCrawling, dsId);
        dsWasHumming_ = ai_.isDeepSeekHumming();
        dsWasCrawling_ = ai_.isDeepSeekCrawling();

        // 有玩偶冲进来了
        if (ai_.reachedPlayer()) {
            triggerJumpscare(ai_.intruderId());
        }
    } else if (state_ == GameState::Jumpscare) {
        jumpscareTimer_ -= dt;
        if (jumpscareTimer_ <= 0.f) {
            state_ = GameState::GameOver;
        }
    }
    // GameOver / Victory / NightIntro：终态，不做任何推进
}

GameSnapshot GameWorld::snapshot() const {
    GameSnapshot s;
    s.state = state_;
    s.night = night_;
    s.hour = currentHour_;
    s.power = power_.level();
    s.leftDoorClosed = leftDoorClosed_;
    s.rightDoorClosed = rightDoorClosed_;
    s.leftDoorArmed = leftArmTimer_ >= kInterceptArmTime;
    s.rightDoorArmed = rightArmTimer_ >= kInterceptArmTime;
    s.leftDetectOn = leftDetectOn_;
    s.rightDetectOn = rightDetectOn_;
    s.leftDoorAI = leftDetectOn_ && ai_.isAtDoor(Route::LeftDoor);
    s.rightDoorAI = rightDetectOn_ && ai_.isAtDoor(Route::RightDoor);
    s.cameraOpen = cameras_.isOpen();
    s.currentCamera = cameras_.current();
    s.deepSeekHumming = ai_.isDeepSeekHumming();
    s.deepSeekCrawling = ai_.isDeepSeekCrawling();
    s.deepSeekNeutralized = ai_.isDeepSeekNeutralized();
    s.animatronics = ai_.states();
    return s;
}

std::vector<GameEvent> GameWorld::drainEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}
