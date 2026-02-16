
#ifndef SUPERGENIUS_SRC_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_HPP

namespace sgns::application {

  /**
   * @brief Application interface for starting the SuperGenius node.
   *
   * Implementations encapsulate initialization and run-loop behavior for the
   * node application.
   */
  class SgnsApplication {
   public:
    virtual ~SgnsApplication() = default;

    /**
     * @brief runs application
     */
    virtual void run() = 0;
  };
}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APPLICATION_HPP
