#!/bin/bash

# 定义要检测的IP列表
#IP_LIST=("10.1.100.1" "10.1.100.2" "10.1.100.6" "10.1.100.7")

IP_FILE="/opt/work/ip_list.txt"  # 假设文件名为 ip_list.txt，你可以根据需要修改
mapfile -t IP_LIST < "$IP_FILE"  # 将文件内容读取到数组 IP_LIST 中

# 每2秒执行一次检测
while true; do
    # 获取当前的BACKEND_LIST
    BACKEND_LIST=$(echo "SHOW_BACKEND_LIST" | nc 127.0.0.1 8080 | awk -F': ' '{print $2}' | tr -d ' ')

    # 初始化正常的IP列表
    HEALTHY_IPS=()

    # 检测每个IP的curl状态码
    for IP in "${IP_LIST[@]}"; do
        #STATUS_CODE=$(curl -o /dev/null -s -w "%{http_code}" "http://$IP")
	STATUS_CODE=$(curl -s -I --connect-timeout 3 --max-time 5 "http://$IP" | awk '{if(NR == 1)print $2}')
        if [ "$STATUS_CODE" == "200" ]; then
            HEALTHY_IPS+=("$IP")
        fi
    done

    # 将正常的IP列表转换为逗号分隔的字符串
    HEALTHY_IPS_STR=$(IFS=,; echo "${HEALTHY_IPS[*]}")

    # 获取当前时间
    CURRENT_TIME=$(date '+%Y-%m-%d %H:%M:%S')

    # 比较BACKEND_LIST和HEALTHY_IPS_STR是否一致
    if [ "$BACKEND_LIST" != "$HEALTHY_IPS_STR" ]; then
        # 如果不一致，打印时间、HEALTHY_IPS_STR和BACKEND_LIST
        echo "[$CURRENT_TIME] BACKEND_LIST和HEALTHY_IPS_STR不一致"
        echo "HEALTHY_IPS_STR: $HEALTHY_IPS_STR"
        echo "BACKEND_LIST: $BACKEND_LIST"
        # 执行UPDATE_BACKEND命令
        echo "UPDATE_BACKEND $HEALTHY_IPS_STR" | nc 127.0.0.1 8080
    else
        # 如果一致，打印时间
        echo "[$CURRENT_TIME] BACKEND_LIST和HEALTHY_IPS_STR一致"
    fi

    # 等待2秒
    sleep 2
done
