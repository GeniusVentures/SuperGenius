
#ifndef SUPERGENIUS_SRC_APP_STATE_MANAGER
#define SUPERGENIUS_SRC_APP_STATE_MANAGER

#include "application/app_state_manager.hpp"

#include <condition_variable>
#include <csignal>
#include <mutex>
#include <queue>

#include "base/logger.hpp"

namespace sgns::application {

  class AppStateManagerImpl : public AppStateManager {
   public:
    AppStateManagerImpl();
    AppStateManagerImpl(const AppStateManagerImpl &) = delete;
    AppStateManagerImpl(AppStateManagerImpl &&) noexcept = delete;

    ~AppStateManagerImpl() override;

    AppStateManagerImpl &operator=(AppStateManagerImpl const &) = delete;
    AppStateManagerImpl &operator=(AppStateManagerImpl &&) noexcept = delete;

    void atPrepare(Callback &&cb) override;
    void atLaunch(Callback &&cb) override;
    void atShutdown(Callback &&cb) override;

    void run() override;
    void shutdown() override;

    State state() const override {
      return state_;
    }

   protected:
    void reset();

    void doPrepare() override;
    void doLaunch() override;
    void doShutdown() override;

   private:
    static std::weak_ptr<AppStateManager> wp_to_myself;
    static void shuttingDownSignalsHandler(int);

    base::Logger logger_;

    State state_ = State::Init;

    std::recursive_mutex mutex_;

    std::mutex cv_mutex_;
    std::condition_variable cv_;

    std::queue<std::function<void()>> prepare_;
    std::queue<std::function<void()>> launch_;
    std::queue<std::function<void()>> shutdown_;

    std::atomic_bool shutdown_requested_{false};
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APP_STATE_MANAGER
