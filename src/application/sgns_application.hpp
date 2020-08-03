
#ifndef SUPERGENIUS_SRC_APPLICATION_HPP
#define SUPERGENIUS_SRC_APPLICATION_HPP

namespace sgns::application {

  /**
   * @class SgnsApplication sgns application interface
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
