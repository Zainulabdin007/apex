#pragma once

#include "apex/types.hpp"

#include <vector>

namespace apex {

// One symbol shard of a price-time matching engine. Orders are pooled and
// linked FIFO inside each price level. The shard owns every order it sees.
class Matcher {
 public:
  Matcher(uint32_t symbols, uint32_t shard, uint32_t threads);

  void reserve(uint64_t orders);
  void on_command(const Command& cmd, Stats& stats, std::vector<Trade>* trades);

  bool has_bid(uint32_t symbol) const;
  bool has_ask(uint32_t symbol) const;
  int32_t best_bid(uint32_t symbol) const;
  int32_t best_ask(uint32_t symbol) const;
  uint64_t bid_volume(uint32_t symbol) const;
  uint64_t ask_volume(uint32_t symbol) const;
  uint32_t resting(uint32_t symbol) const;

 private:
  struct Order {
    uint64_t id = 0;
    int32_t price = 0;
    uint32_t qty = 0;
    uint32_t prev = kNil;
    uint32_t next = kNil;
    uint8_t side = kBuy;
    uint8_t live = 0;
  };

  struct Level {
    uint32_t head = kNil;
    uint32_t tail = kNil;
    uint64_t volume = 0;
  };

  struct Slot {
    uint64_t id = 0;
    uint32_t order = kNil;
    uint8_t state = 0;
  };

  class Side {
   public:
    explicit Side(bool bids);
    bool empty() const;
    int32_t best() const;
    Level& front();
    uint64_t volume() const;
    int find(int32_t price) const;
    Level& level_at(int index);
    Level& level_for(int32_t price);
    void erase_at(int index);

   private:
    bool bids_;
    std::vector<int32_t> prices_;
    std::vector<Level> levels_;
    int insert_pos(int32_t price) const;
  };

  bool owns(uint32_t symbol) const;
  uint32_t book_index(uint32_t symbol) const;
  void cancel(const Command& cmd, Stats& stats);
  void take(Command cmd, bool limit, Stats& stats, std::vector<Trade>* trades);
  void rest(uint32_t book, const Command& cmd, Stats& stats);
  void consume_level(Side& opposite, uint32_t book, uint32_t& qty, int32_t price, const Command& taker,
                     Stats& stats, std::vector<Trade>* trades);
  uint32_t pool_alloc(const Order& proto);
  void pool_free(uint32_t index);
  Order& order_at(uint32_t index);
  void id_put(uint64_t id, uint32_t order);
  uint32_t id_find(uint64_t id) const;
  void id_erase(uint64_t id);
  void id_grow();
  static uint32_t mix(uint64_t id);

  uint32_t symbols_;
  uint32_t shard_;
  uint32_t threads_;
  std::vector<Order> orders_;
  std::vector<uint32_t> free_;
  std::vector<Slot> slots_;
  uint32_t mask_ = 0;
  uint32_t live_ = 0;
  uint32_t used_ = 0;
  std::vector<Side> bids_;
  std::vector<Side> asks_;
  std::vector<uint32_t> resting_;
};

}  // namespace apex
