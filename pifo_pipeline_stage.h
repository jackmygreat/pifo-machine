#ifndef PIFO_PIPELINE_STAGE_H_
#define PIFO_PIPELINE_STAGE_H_

#include <cstdint>

#include <map>
#include <functional>

// Banzai headers
#include "field_container.h"

#include "convenience_typedefs.h"
#include "priority_queue.h"
#include "calendar_queue.h"

/// enum to distinguish between priority and calendar queues
/// This lets us keep each in its own distinct namespace without
/// having to create a polymorphic queue class
enum class QueueType {
  PRIORITY_QUEUE,
  CALENDAR_QUEUE
};

/// Opcode class, specify whether we are doing
/// an enqueue / dequeue / transmit.
enum class Operation {
  ENQ,
  DEQ,
  TRANSMIT
};

/// Arguments to enqueue or dequeue
/// from a particular stage
/// in the PIFO pipeline
struct PIFOArguments {
  /// Which stage to enqueue or dequeue from
  uint32_t  stage_id;

  /// Queue type (calendar / prio. q) to enqueue or dequeue from
  QueueType q_type;

  /// Queue id to enqueue or dequeue from
  uint32_t  queue_id;
};

/// Next hop information, what operation, which stage,
/// which queue type and which queue id should we be
/// sending this PIFOPacket to?
struct NextHop {
  /// Operation: ENQ/DEQ/TRANSMIT
  Operation op;

  /// Vector of PIFOArguments
  /// We use a vector because for an Operation::ENQ,
  /// we might need to insert it into multiple stages
  std::vector<PIFOArguments> pifo_arguments;
};

/// Simple look-up table to look-up a packet's next hop.
/// Takes as input a packet field name as a string
/// and a std::map that determines the next-hop based on that
/// field name. TODO: We assume all fields are ints
class LookUpTable {
 public:
  LookUpTable(const std::string & lut_field_name, const std::initializer_list<std::pair<const int, NextHop>> & lut_init)
      : look_up_field_name_(lut_field_name),
        look_up_table_(lut_init) {}

  /// Lookup a PIFOPacket in a LookUpTable using a specific field name
  auto lookup(const PIFOPacket & packet) const { return look_up_table_.at(packet(look_up_field_name_)); }

 private:
  /// Field name to use for lookup
  const std::string look_up_field_name_ = "";

  /// Lookup table itself
  const std::map<int, NextHop> look_up_table_ = {};
};

/// PIFOPipelineStage models a stage of PIFOs
/// ---each of which can be a priority queue or a calendar queue.
/// On any tick, there can be at most one enqueue and one dequeue
/// to the PIFOPipelineStage using the enq and deq methods.
/// These enq and deq methods can be external or from adjacent stages.
/// A lookup table within each stage tells each packet where to go next.
/// The compiler fills in the lut based on the graphviz dot file
/// describing the scheduling hierarchy.
class PIFOPipelineStage {
 public:
  typedef PushableElement<PIFOPacket, priority_t> PushablePIFOPacket;

  /// Constructor for PIFOPipelineStage with a number of prio. and cal. qs
  PIFOPipelineStage(const uint32_t & num_prio_queues,
                    const uint32_t & num_cal_queues,
                    const std::string & lut_field_name,
                    const std::initializer_list<std::pair<const int, NextHop>> & lut_initializer,
                    const std::function<priority_t(PIFOPacket)> & t_prio_computer)
      : priority_queue_bank_(num_prio_queues),
        calendar_queue_bank_(num_cal_queues),
        next_hop_lut_(lut_field_name, lut_initializer),
        prio_computer_(t_prio_computer) {}

  /// Enqueue
  /// These happen externally from the ingress pipeline
  /// or from a push from a calendar queue/
  void enq(const QueueType & q_type, const uint32_t & queue_id,
           const PIFOPacket & packet, const uint32_t & tick) {
    const auto prio = prio_computer_(packet);
    if (q_type == QueueType::PRIORITY_QUEUE) {
      priority_queue_bank_.at(queue_id).enq(packet,
                                            prio, tick);
    } else {
      calendar_queue_bank_.at(queue_id).enq(packet,
                                            prio, tick);
    }
  }

  /// Dequeues
  /// Happen implicitly starting from the root PIFO
  Optional<PIFOPacket> deq(const QueueType & q_type, const uint32_t & queue_id,
                            const uint32_t & tick) {
    if (q_type == QueueType::PRIORITY_QUEUE) {
      return priority_queue_bank_.at(queue_id).deq(tick);
    } else {
      return calendar_queue_bank_.at(queue_id).deq(tick);
    }
  }

  /// Overload stream insertion operator
  friend std::ostream & operator<<(std::ostream & out, const PIFOPipelineStage & pipe_stage) {
    out << "Contents of PIFOPipelineStage " << std::endl;
    out << "Priority Queues: " << std::endl;
    for (uint32_t i = 0; i < pipe_stage.priority_queue_bank_.size(); i++) {
      out << "Index " << i << " " << pipe_stage.priority_queue_bank_.at(i) << std::endl;
    }

    out << "Calendar Queues: " << std::endl;
    for (uint32_t i = 0; i < pipe_stage.calendar_queue_bank_.size(); i++) {
      out << "Index " << i << " " << pipe_stage.calendar_queue_bank_.at(i) << std::endl;
    }
    out << "End of contents of PIFOPipelineStage " << std::endl;

    return out;
  }

  /// Find "next hop" after a dequeue
  auto find_next_hop(const PIFOPacket & packet) const { return next_hop_lut_.lookup(packet); }

 private:
  /// Bank of priority queues
  std::vector<PriorityQueue<PIFOPacket, priority_t>> priority_queue_bank_;

  /// Bank of calendar queues
  std::vector<CalendarQueue<PIFOPacket, priority_t>> calendar_queue_bank_;

  /// look-up table to find the next hop
  const LookUpTable next_hop_lut_;

  /// Function object to compute incoming packet's priority
  /// Identity function by default
  const std::function<priority_t(PIFOPacket)> prio_computer_;
};

#endif  // PIFO_PIPELINE_STAGE_H_
