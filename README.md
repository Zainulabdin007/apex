# Apex

Apex is a C++ price-time matching engine. It shards symbols across cores, keeps each book on one thread, and matches limit orders, market orders, and cancels from an in-memory stream. Fills can be written as CSV and ingested by [Strata](https://github.com/Zainulabdin007/Strata).

The design uses ordinary exchange techniques: price-time priority, symbol sharding, an order pool, FIFO lists inside each price level, and an open-addressing map from order id to that pool. It is not an implementation of any firm's private trading system.

## Build

```sh
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -I include \
  src/matcher.cpp src/synth.cpp src/main.cpp -o build/apex -pthread
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -I include \
  src/matcher.cpp src/synth.cpp tests/test_matcher.cpp -o build/apex_tests -pthread
./build/apex_tests
```

With CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/apex_tests
```

## Run

`apex bench` builds a synthetic order stream and matches it. The default leaves one core free. On this 18-core machine that is 17 matcher threads. Each symbol lives on `symbol % threads`, so a book is never shared.

Books stay bounded: a symbol holds at most 64 resting orders. The generator mixes passive limits, crossing limits, cancels, and market orders around a random-walk mid price.

```sh
./build/apex bench --symbols 4096 --orders 32000000 --threads 17
./build/apex bench --symbols 1024 --orders 4000000 --threads 1
```

Measured on this MacBook Pro (M5 Pro, 18 cores, 64 GB), release build, orders already in memory:

| Run | Commands | Trades | Match time | Commands/s | Trades/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| 17 threads, 4096 symbols, 32M orders | 47,877,376 | 16,247,917 | 0.215 s | 222.4M | 75.5M |
| 1 thread, 1024 symbols, 4M orders | 5,966,103 | 2,028,852 | 0.326 s | 18.3M | 6.2M |

Command count is higher than `--orders` because a full local ring also emits a cancel. This measures matching only. It does not include a network gateway, kernel bypass, or decoding a market-data feed.

A larger run fits in 64 GB. `--orders 100000000` is a few gigabytes of commands plus the order pools. The per-symbol resting cap is what keeps the books from growing with the stream length.

## Strata

`apex export` writes fills in the column layout Strata already ingests:

```text
seq,ts,symbol,side,price,size,is_implied,payload
```

```sh
./build/apex export --orders 20000 --symbols 32 --output /tmp/apex-trades.csv
cargo run -p strata-cli --release --manifest-path /path/to/Strata/Cargo.toml -- \
  ingest /tmp/apex-trades.csv \
  --profile /path/to/apex/profile/apex-trades.toml \
  --output /tmp/apex-book
```

A 20,000-order export produced 9,908 fills. Strata accepted that CSV, compressed it, and `strata verify` reported the segment healthy.
