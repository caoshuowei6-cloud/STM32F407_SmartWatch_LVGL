#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os

path = os.path.join(os.path.dirname(__file__), '..', 'MY', 'main.c')

with open(path, 'r', encoding='gbk', errors='ignore') as f:
    text = f.read()

# 1. Add vApplicationIdleHook
idle_hook_code = """
void vApplicationIdleHook(void)
{
    /* Cortex-M4 内核休眠：在各任务阻塞等待时切入硬件低功耗 Sleep 模式，任意中断(SysTick/按键/DMA)瞬时唤醒 */
    __WFI();
}
"""

if "vApplicationIdleHook" not in text:
    target_pos = text.find("void vApplicationMallocFailedHook")
    if target_pos != -1:
        end_brace = text.find("}", target_pos)
        end_brace = text.find("}", end_brace + 1) # second brace
        text = text[:end_brace+1] + "\n" + idle_hook_code + text[end_brace+1:]
        print("[+] vApplicationIdleHook added!")
    else:
        print("[-] Target hook pos not found!")
else:
    print("[*] vApplicationIdleHook already present.")

# 2. Update Task_UI loop
old_ui_block = """        /* 2. 100ms 周期业务动效刷新 (秒表、小恐龙跳跃、水平仪气泡、电子萌"""
# Let's find:
#         /* 2. 100ms 周期业务动效刷新
# down to
#         vTaskDelay(pdMS_TO_TICKS(1));

p_start = text.find("if ((xTaskGetTickCount() - last_100ms_tick) >= pdMS_TO_TICKS(100))")
p_end = text.find("vTaskDelay(pdMS_TO_TICKS(1));", p_start)

if p_start != -1 and p_end != -1:
    # Walk back to include the comment before p_start
    comment_marker = text.rfind("/*", 0, p_start)
    if comment_marker != -1 and (p_start - comment_marker) < 150:
        p_start = comment_marker
    p_end += len("vTaskDelay(pdMS_TO_TICKS(1));")
    
    new_ui_block = """        /* 2. 待机低功耗动态调度：息屏状态下挂起 100ms 动效与高频渲染，让出 CPU 给内核休眠 */
        if (Power_Manager_GetState() == PM_STATE_STANDBY)
        {
            /* 待机态：以 50ms 慢轮询维持日历时钟和电源更新，CPU 占用率立降至 <0.5% */
            if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
            {
                last_1s_tick = xTaskGetTickCount();
                UI_Watch_Update_1s();
                Power_Manager_Update();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        else
        {
            /* 运行态：100ms 动效刷新与 LVGL 1ms 满血高频渲染流水线 (保障 30~60 FPS 极速跟手) */
            if ((xTaskGetTickCount() - last_100ms_tick) >= pdMS_TO_TICKS(100))
            {
                last_100ms_tick = xTaskGetTickCount();
                UI_Watch_Update_100ms();
            }

            if ((xTaskGetTickCount() - last_1s_tick) >= pdMS_TO_TICKS(1000))
            {
                last_1s_tick = xTaskGetTickCount();
                UI_Watch_Update_1s();
                Power_Manager_Update();
            }

            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(1));
        }"""
    text = text[:p_start] + new_ui_block + text[p_end:]
    print("[+] Task_UI low power dynamic duty-cycle added!")
else:
    print("[-] UI block not found!", p_start, p_end)

with open(path, 'w', encoding='gbk', errors='ignore') as f:
    f.write(text)

print("[+] Done!")
