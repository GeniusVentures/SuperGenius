
#ifndef SUPERGENIUS_APPLICATION_CONFIG_READER_ERROR_HPP
#define SUPERGENIUS_APPLICATION_CONFIG_READER_ERROR_HPP

#include <outcome/outcome.hpp>

namespace sgns::application {

  /**
   * Codes for errors that originate in configuration readers
   */
  enum class ConfigReaderError {
    MISSING_ENTRY = 1,
    PARSER_ERROR,
    NOT_YET_IMPLEMENTED
  };

}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::application, ConfigReaderError);

#endif  // SUPERGENIUS_APPLICATION_CONFIG_READER_ERROR_HPP
