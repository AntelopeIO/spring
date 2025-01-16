(module
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32)))
   (memory 1)
   (export "apply" (func $apply))

   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64) (local $end i64)
      (drop (call $read_action_data (i32.const 4) (i32.const 8)))
      (set_local $end (i64.load (i32.const 4)))

      (loop
         (set_global $i (i64.add (i64.const 1) (get_global $i)))
         (br_if 0 (i64.ne (get_local $end) (get_global $i)))
      )
   )

   (global $i (mut i64) (i64.const 0))
)