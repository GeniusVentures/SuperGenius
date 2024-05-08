#include "injector/application_injector.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::injector, InjectorError, e) {
  using E = sgns::injector::InjectorError;
  switch (e) {
    case E::INDEX_OUT_OF_BOUND:
      return "index out of bound";
  }
  return "Unknown error";
}
