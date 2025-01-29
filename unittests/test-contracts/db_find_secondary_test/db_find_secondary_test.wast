(module
   (import "env" "db_idx64_store" (func $db_idx64_store (param i64 i64 i64 i64 i32) (result i32)))
   (import "env" "db_idx64_find_secondary" (func $db_idx64_find_secondary (param i64 i64 i64 i32 i32) (result i32)))
   (import "env" "db_idx128_store" (func $db_idx128_store (param i64 i64 i64 i64 i32) (result i32)))
   (import "env" "db_idx128_find_secondary" (func $db_idx128_find_secondary (param i64 i64 i64 i32 i32) (result i32)))
   (import "env" "db_idx256_store" (func $db_idx256_store (param i64 i64 i64 i64 i32 i32) (result i32)))
   (import "env" "db_idx256_find_secondary" (func $db_idx256_find_secondary (param i64 i64 i64 i32 i32 i32) (result i32)))
   (import "env" "db_idx_double_store" (func $db_idx_double_store (param i64 i64 i64 i64 i32) (result i32)))
   (import "env" "db_idx_double_find_secondary" (func $db_idx_double_find_secondary (param i64 i64 i64 i32 i32) (result i32)))
   (import "env" "db_idx_long_double_store" (func $db_idx_long_double_store (param i64 i64 i64 i64 i32) (result i32)))
   (import "env" "db_idx_long_double_find_secondary" (func $db_idx_long_double_find_secondary (param i64 i64 i64 i32 i32) (result i32)))

   (memory 1)
   (export "apply" (func $apply))

   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      ;;;;;;;;;;idx64
      ;;load 2 items with sec key at 16
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 7)  (i32.const 16)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 14) (i32.const 16)))
      ;;load items with sec key at 48 -- this is what will be tested against
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 15) (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 8)  (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 1)  (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 4)  (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 90) (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 42) (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 3)  (i32.const 48)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 60) (i32.const 48)))
      ;;load 2 items with sec key at 80
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0)  (i32.const 80)))
      (drop (call $db_idx64_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 88) (i32.const 80)))

      ;;;;;;;;;;idx128
      ;;load 2 items with sec key at 16
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 7)  (i32.const 16)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 14) (i32.const 16)))
      ;;load items with sec key at 48 -- this is what will be tested against
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 15) (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 8)  (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 1)  (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 4)  (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 90) (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 42) (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 3)  (i32.const 48)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 60) (i32.const 48)))
      ;;load 2 items with sec key at 80
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0)  (i32.const 80)))
      (drop (call $db_idx128_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 88) (i32.const 80)))

      ;;;;;;;;;;idx256
      ;;load 2 items with sec key at 16
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 7)  (i32.const 16) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 14) (i32.const 16) (i32.const 2)))
      ;;load items with sec key at 48 -- this is what will be tested against
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 15) (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 8)  (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 1)  (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 4)  (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 90) (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 42) (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 3)  (i32.const 48) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 60) (i32.const 48) (i32.const 2)))
      ;;load 2 items with sec key at 80
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0)  (i32.const 80) (i32.const 2)))
      (drop (call $db_idx256_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 88) (i32.const 80) (i32.const 2)))

      ;;;;;;;;;;double
      ;;load 2 items with sec key at 16
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 7)  (i32.const 16)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 14) (i32.const 16)))
      ;;load items with sec key at 48 -- this is what will be tested against
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 15) (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 8)  (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 1)  (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 4)  (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 90) (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 42) (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 3)  (i32.const 48)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 60) (i32.const 48)))
      ;;load 2 items with sec key at 80
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0)  (i32.const 80)))
      (drop (call $db_idx_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 88) (i32.const 80)))

      ;;;;;;;;;;long double (f128)
      ;;load 2 items with sec key at 16
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 7)  (i32.const 16)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 14) (i32.const 16)))
      ;;load items with sec key at 48 -- this is what will be tested against
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 15) (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 8)  (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 1)  (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 4)  (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 90) (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 42) (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 3)  (i32.const 48)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 60) (i32.const 48)))
      ;;load 2 items with sec key at 80
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0)  (i32.const 80)))
      (drop (call $db_idx_long_double_store (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 88) (i32.const 80)))

      ;;test each index type. expect to find primary key of 1 when searching seckey at 48 since 1 was the lowest that was stored for the
      ;; searched seckey (the primary key will be placed at memory location 1024)
      (drop (call $db_idx64_find_secondary (get_local $receiver) (get_local $receiver) (get_local $receiver) (i32.const 48) (i32.const 1024)))
      (if (i64.ne (i64.load (i32.const 1024)) (i64.const 1)) (then unreachable))
      (drop (call $db_idx128_find_secondary (get_local $receiver) (get_local $receiver) (get_local $receiver) (i32.const 48) (i32.const 1024)))
      (if (i64.ne (i64.load (i32.const 1024)) (i64.const 1)) (then unreachable))
      (drop (call $db_idx256_find_secondary (get_local $receiver) (get_local $receiver) (get_local $receiver) (i32.const 48) (i32.const 2) (i32.const 1024)))
      (if (i64.ne (i64.load (i32.const 1024)) (i64.const 1)) (then unreachable))
      (drop (call $db_idx_double_find_secondary (get_local $receiver) (get_local $receiver) (get_local $receiver) (i32.const 48) (i32.const 1024)))
      (if (i64.ne (i64.load (i32.const 1024)) (i64.const 1)) (then unreachable))
      (drop (call $db_idx_long_double_find_secondary (get_local $receiver) (get_local $receiver) (get_local $receiver) (i32.const 48) (i32.const 1024)))
      (if (i64.ne (i64.load (i32.const 1024)) (i64.const 1)) (then unreachable))
   )

   ;;secondary keys to be used
   (data (i32.const 16) "\00\00\00\00\01\02\03\04AbcdefghABCDEFGH01234567")
   (data (i32.const 48) "\00\00\00\00\01\02\03\05abcdefghABCDEFGH99999999")
   (data (i32.const 80) "\00\00\00\00\01\02\03\06aBcdefghABCDEFGH00000000")
)
