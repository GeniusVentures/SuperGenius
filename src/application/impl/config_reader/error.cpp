
#include "application/impl/config_reader/error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::application,
                            ConfigReaderError,
                            e) {
  using E = sgns::application::ConfigReaderError;
  switch (e) {
    case E::MISSING_ENTRY:
      return "A required entry is missing in the provided config file";
    case E::PARSER_ERROR:
      return "Internal parser error";
    case E::NOT_YET_IMPLEMENTED:
      return "Known entry name, but parsing not implemented";
  }
  return "Unknown error";
}
