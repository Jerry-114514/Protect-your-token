#pragma once

#include <cstdint>
#include <random>
#include <vector>

// 游戏顶层状态机
enum class GameState {
    NightIntro,
    Playing,
    PowerOutage,
    Jumpscare,
    GameOver,
    Victory
};

// 玩家命令：输入层 -> 逻辑层
enum class CommandType {
    ToggleLeftDoor,    // 拦截系统：左门
    ToggleRightDoor,   // 拦截系统：右门
    ToggleLeftDetect,  // 检测系统：左门
    ToggleRightDetect, // 检测系统：右门
    OpenCamera,
    CloseCamera,
    SelectCamera,
    ApplyBasin         // 给 DeepSeek 带铁盆
};

struct Command {
    CommandType type;
    int cameraId = -1; // 仅 SelectCamera 使用
};

// 逻辑层 -> 表现层的事件
enum class GameEventType {
    HourChanged,
    PowerOut,
    JumpscareTriggered,
    Victory,
    AnimatronicMoved,
    DoorSound,
    CameraNoise,
    Noise,              // DouBao 移动的声响
    DeepSeekHumming,    // DeepSeek 开始哼歌
    DeepSeekCrawling,   // 哼歌结束未处理，DeepSeek 从管道进攻（不可拦截）
    DeepSeekNeutralized // 铁盆成功带上
};

struct GameEvent {
    GameEventType type;
    int animatronicId = -1;
    int room = -1;
};

// 玩偶的进攻路线：左门、右门、通风管道
enum class Route {
    LeftDoor,
    RightDoor,
    Vent
};

// 玩偶角色（id 与枚举值一一对应，见 roster）
enum class AnimatronicType {
    GPT      = 0, // GPT 6 Astra（Freddy）
    Claude   = 1, // Claude Fable 5.2（Bonnie 兔子）
    QWen     = 2, // QWen 4 Max（Chica 鸭子）
    DouBao   = 3, // DouBao（Foxy）
    DeepSeek = 4  // DeepSeek V4.1 Flash（走管道）
};

// 角色名（渲染层/日志用）
const char* animatronicName(AnimatronicType type);

// 单个玩偶的当前状态（渲染层据此决定画谁、画在哪）
struct AnimatronicState {
    int id = 0;
    AnimatronicType type = AnimatronicType::GPT;
    Route route = Route::LeftDoor; // 走左门/右门/管道
    int room = 0;                  // 0=最远，kPathLength-1=贴到入口
    bool atDoor = false;           // 已到入口（门 / 管道口）
};

// 快照：每帧拷给渲染层的只读数据
struct GameSnapshot {
    GameState state = GameState::NightIntro;
    int night = 1;
    int hour = 0;                // 0~6，对应 12AM~6AM
    float power = 100.f;         // 0~100（预留 token）

    // 拦截系统（门）
    bool leftDoorClosed = false;
    bool rightDoorClosed = false;
    bool leftDoorArmed = false;  // 拦截已生效（开启 >= 3 秒）
    bool rightDoorArmed = false;

    // 检测系统
    bool leftDetectOn = false;
    bool rightDetectOn = false;
    bool leftDoorAI = false;     // 检测到左门口有 AI（仅检测开启时有效）
    bool rightDoorAI = false;

    // 摄像头
    bool cameraOpen = false;
    int currentCamera = -1;

    // DeepSeek 特殊阶段
    bool deepSeekHumming = false;
    bool deepSeekCrawling = false;
    bool deepSeekNeutralized = false;

    std::vector<AnimatronicState> animatronics;
};

// 电力（预留 token）：基础消耗 + 每种系统额外消耗，归零则停电
class PowerSystem {
public:
    void reset();
    void update(float dt,
                bool leftDoorClosed, bool rightDoorClosed,
                bool leftDetectOn, bool rightDetectOn,
                bool cameraOpen);
    float level() const { return static_cast<float>(power_); }
    bool isOut() const { return power_ <= 0.f; }

private:
    double power_ = 100.0;

    // 每秒消耗（可调平衡）
    static constexpr double kBaseDrain   = 0.15; // 基础
    static constexpr double kDoorDrain   = 0.07; // 每扇拦截中的门
    static constexpr double kDetectDrain = 0.02; // 每侧检测系统
    static constexpr double kCameraDrain = 0.10; // 开着摄像头时
};

// 玩偶 AI：4 个普通玩偶走门（各自不同行为）+ DeepSeek 特殊机制
class AnimatronicAI {
public:
    void reset(int night);
    void seed(std::uint32_t value) { rng_.seed(value); }
    void update(float dt, float nightElapsed, int watchedRoom,
                bool leftDoorArmed, bool rightDoorArmed);

    bool reachedPlayer() const { return reachedPlayer_; }
    int intruderId() const { return intruderId_; }
    const std::vector<AnimatronicState>& states() const { return states_; }
    const std::vector<int>& movedIds() const { return movedIds_; }

    // 检测系统用：某扇门口是否有玩偶
    bool isAtDoor(Route route) const;

    // DeepSeek 特殊机制
    bool isDeepSeekHumming() const;
    bool isDeepSeekCrawling() const;
    bool isDeepSeekNeutralized() const;
    int deepSeekRoom() const;
    bool applyBasin(int watchedRoom); // 摄像头正对 DeepSeek 时带铁盆成功返回 true

    static constexpr int kPathLength = 5;      // 房间 0..4，4 = 到入口
    static constexpr int kMaxAnimatronics = 5; // 5 个角色：夜 1~5 逐个登场

private:
    enum class DeepSeekPhase { Dormant, Humming, Neutralized, Crawling };

    void updateDeepSeek(float dt, float nightElapsed);

    std::vector<AnimatronicState> states_;
    std::vector<float> moveTimers_;    // 每个玩偶自己的移动计时
    std::vector<float> retreatTimers_; // 每个玩偶自己的僵持计时
    std::vector<float> attackTimers_;  // 到门口后留给玩家的应对时间
    std::mt19937 rng_{std::random_device{}()};

    float aggression_ = 0.3f; // 每次尝试成功前进的概率，随 night 上升

    bool reachedPlayer_ = false;
    int intruderId_ = -1;
    std::vector<int> movedIds_;

    DeepSeekPhase deepSeekPhase_ = DeepSeekPhase::Dormant;
    float deepSeekHumTimer_ = 0.f;
    float deepSeekCrawlTimer_ = 0.f;
    int deepSeekVentSteps_ = 0;

    static constexpr float kRetreatDelay = 3.f;          // 被门挡住后多久退回
    static constexpr float kAttackDelay = 5.f;           // 到门口后多久能够入侵
    static constexpr float kHummingDelay = 15.f;         // 开局多久后开始哼歌
    static constexpr float kHummingDuration = 12.f;      // 哼歌窗口（限时）
    static constexpr float kDeepSeekCrawlInterval = 3.5f;
    static constexpr int kDeepSeekVentSteps = 3;
};

// 摄像头：当前切到哪个摄像头、是否开着（这里摄像头 id == 房间 id，一一对应）
class CameraSystem {
public:
    void reset();
    void open()   { open_ = true; }
    void close()  { open_ = false; }
    void select(int cameraId);

    bool isOpen() const { return open_; }
    int current() const { return current_; }
    bool isObserving(int room) const { return open_ && current_ == room; }

    static constexpr int kCameraCount = 5;

private:
    bool open_ = false;
    int current_ = -1;
};

// 游戏世界：串起各子系统，跑状态机 + 时间 + 事件
class GameWorld {
public:
    void startNight(int night);
    void startNight(int night, std::uint32_t seed);
    void handleCommand(const Command& cmd);
    void update(float dt);

    GameSnapshot snapshot() const;        // 按值返回，安全拷给渲染层
    std::vector<GameEvent> drainEvents(); // 取走并清空事件

private:
    void emit(GameEventType type, int animatronicId = -1, int room = -1);
    void triggerJumpscare(int animatronicId);
    void advanceStep();
    void disableDefenses();

    GameState state_ = GameState::NightIntro;
    int night_ = 1;
    float nightElapsed_ = 0.f;
    double pendingTime_ = 0.0;
    int currentHour_ = 0;

    bool leftDoorClosed_ = false;
    bool rightDoorClosed_ = false;
    float leftArmTimer_ = 0.f;
    float rightArmTimer_ = 0.f;
    bool leftDetectOn_ = false;
    bool rightDetectOn_ = false;

    float jumpscareTimer_ = 0.f;

    // DeepSeek 在上一逻辑步的阶段（用于发事件）
    bool dsWasHumming_ = false;
    bool dsWasCrawling_ = false;

    PowerSystem power_;
    AnimatronicAI ai_;
    CameraSystem cameras_;

    std::vector<GameEvent> events_;

    static constexpr float kHourDuration = 60.f;      // 每小时秒数（demo 取短）
    static constexpr float kStep = 1.f / 32.f;        // 固定逻辑步长（秒）
    static constexpr float kJumpscareDuration = 3.f;
    static constexpr float kInterceptArmTime = 3.f;   // 拦截生效需要 3 秒
};
