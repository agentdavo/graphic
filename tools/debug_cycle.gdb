set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
break vkmin_frame_begin
run --frame 0 --out build/boundaries/debug-cycle.png
step
bt
next
info locals
finish
break vkmin_frame_end
continue
step
bt
next
finish
continue
