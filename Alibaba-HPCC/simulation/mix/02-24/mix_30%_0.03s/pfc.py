import subprocess
import os

def read_fct_last_line():
    """读取 fct.txt 的最后一行，提取第6列和第7列"""
    try:
        # 使用 tail -n 3 读取最后三行，确保最后一行是完整的
        result = subprocess.check_output(['tail', '-n', '3', './fct.txt']).decode('utf-8').strip()
        last_line = result.split('\n')[-1]
        columns = last_line.split()  # 假设列之间以空格分隔
        col7 = float(columns[5]) if len(columns) > 6 else None
        col8 = float(columns[6]) if len(columns) > 7 else None
        return col7, col8
    except Exception as e:
        print(f"Error reading fct.txt: {e}")
        return None, None

def process_pfc_events(start_time, end_time):
    """处理 pfc.txt 文件中的事件，计算暂停时间占比"""
    pause_time = 0.0  # 累计暂停时间
    start_time = float(start_time)
    end_time = float(end_time)
    events = []

    # 读取 pfc.txt 文件
    try:
        with open('./pfc.txt', 'r') as f:
            for line in f:
                parts = line.strip().split()  # 假设列之间以空格分隔
                if len(parts) >= 5:
                    timestamp = float(parts[0])
                    event = parts[4]  # 第5列
                    events.append({'time': timestamp, 'event': event})
    except Exception as e:
        print(f"Error reading pfc.txt: {e}")
        return 0.0

    # 按时间排序事件
    events.sort(key=lambda x: x['time'])

    # 维护暂停栈
    pauses = []
    current_pause = None

    for event in events:
        time = event['time']
        event_type = event['event']
        if event_type == '1':  # 暂停事件
            if current_pause is None and time >= start_time and time <= end_time:
                current_pause = time
                pauses.append(current_pause)
        elif event_type == '0':  # 恢复事件
            if current_pause is not None:
                # 计算暂停时间
                pause_start = pauses.pop(0)
                pause_duration = time - pause_start
                pause_time += pause_duration
                current_pause = None if not pauses else pauses[-1]

    # 处理未恢复的暂停事件
    if pauses:
        for pause in pauses:
            pause_duration = end_time - pause
            pause_time += pause_duration

    total_time = end_time - start_time
    print(f"Total time: {total_time:.2f} ms")
    print(f"Pause time: {pause_time:.2f} ms")
    pause_percentage = (pause_time / total_time) * 100 if total_time != 0 else 0.0

    return pause_percentage

if __name__ == "__main__":
    # 读取 fct.txt 的最后一行，提取第7列和第8列
    col7, col8 = read_fct_last_line()
    if col7 is None or col8 is None:
        print("Error: Could not read columns 7 and 8 from fct.txt.")
    else:
        end_time = col7 + col8
        print(f"End time: {end_time:.2f} ms")
        start_time = 2000000000

        # 计算暂停时间占比
        pause_percentage = process_pfc_events(start_time, end_time)
        print(f"Pause time percentage: {pause_percentage:.2f}%")