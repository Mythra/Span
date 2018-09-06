#ifndef SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_DETAIL_SLOT_CALL_ITERATOR_HH_
#define SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_DETAIL_SLOT_CALL_ITERATOR_HH_

#include <iterator>

namespace slimsig {
  namespace detail {
    template <class Iterator>
    class slot_call_iterator : std::iterator<std::forward_iterator_tag> { };
  }  // namespace detail
}  // namespace slimsig

#endif  // SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_DETAIL_SLOT_CALL_ITERATOR_HH_
