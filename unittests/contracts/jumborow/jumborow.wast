(module
   (import "env" "db_store_i64" (func $db_store_i64 (param i64 i64 i64 i64 i32 i32) (result i32)))
   (memory $0 528)
   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $db_store_i64 (get_local $receiver) (get_local $receiver) (get_local $receiver) (i64.const 0) (i32.const 1) (i32.const 34603007)))
   )
)
