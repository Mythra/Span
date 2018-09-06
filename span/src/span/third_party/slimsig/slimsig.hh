#ifndef SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_SLIMSIG_HH_
#define SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_SLIMSIG_HH_

#include "./detail/signal_base.hh"

namespace slimsig {
  template <class Handler, class SignalTraits = signal_traits<Handler>, class Allocator = std::allocator<std::function<Handler>>>
  class signal : private signal_base<SignalTraits, Allocator, Handler> {
  public:
    using base = signal_base<SignalTraits, Allocator, Handler>;
    using typename base::return_type;
    using typename base::callback;
    using typename base::allocator_type;
    using typename base::slot;
    using typename base::slot_list;
    using typename base::list_allocator_type;
    using typename base::const_slot_reference;
    using typename base::connection;
    using base::arity;
    using base::argument;
    // allocator constructor
    using base::base;

    // default constructor
    signal() : signal(allocator_type()) {};
    using base::emit;
    using base::connect;
    using base::connect_once;
    using base::connect_extended;
    using base::disconnect_all;
    using base::slot_count;
    using base::get_allocator;
    using base::empty;
    using base::swap;
    using base::max_size;
    using base::max_depth;
    using base::get_depth;
    using base::is_running;
    using base::remaining_slots;

  };
  template <
    class Handler,
    class SignalTraits = signal_traits<Handler>,
    class Allocator = std::allocator<std::function<Handler>>
  > using signal_t = signal<Handler, SignalTraits, Allocator>;

}  // namespace slimsig

#endif  // SPAN_SRC_SPAN_THIRD_PARTY_SLIMSIG_SLIMSIG_HH_
