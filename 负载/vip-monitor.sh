#!/bin/bash

# 配置
IFACE="ens160"           # 网络接口，根据实际情况调整
VIP="172.16.100.5"   # 虚拟 IP
SLEEP_INTERVAL=5     # 检测间隔（秒）
ROLE=master


# 函数：检查本机是否配置了虚拟 IP
check_vip() {
    ip addr show dev "$IFACE" | grep -q "$VIP"
    return $?
}

# 函数：添加虚拟 IP
add_vip() {
    sudo ip addr add "$VIP/24" dev "$IFACE"
    echo "$(date) - Added VIP $VIP on $LOCAL_IP" >&2
}

ClearRouteArp(){
    hostname=$(hostname)
    c=$(curl -s "http://10.1.100.142/clear_arp?target_ip=${VIP}&hostname${hostname}")
    echo -e "$c"
}

# 主循环
while true; do
    CURRENT_TIME=$(date '+%Y-%m-%d %H:%M:%S')
    if [ "$ROLE" = "master" ]; then
        if ! check_vip; then
            add_vip
            ClearRouteArp
        else
            echo "[$CURRENT_TIME] 当前主机正常，不需添加vip"
       fi
    fi
    sleep "$SLEEP_INTERVAL"
done
