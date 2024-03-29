#include "net.h"
#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"

/**
 * @brief 处理一个收到的数据包
 * 
 * @param buf 要处理的数据包
 * @param src_mac 源mac地址
 */
void ip_in(buf_t *buf, uint8_t *src_mac)
{
    // TO-DO
    if (buf->len < 20)                                  // buf 长度  >= 最小 ip 头部长
        return;
    
    ip_hdr_t *hdr = (ip_hdr_t *)buf->data;

    if ((hdr->tos & 0x1) != 0 ||                        // TOS 保留位 = 0
        hdr->version != IP_VERSION_4 ||                 // IP 版本    = 4
        swap16(hdr->total_len16) > buf->len ||          // 总长度字段 <= buf 长度
        hdr->hdr_len * IP_HDR_LEN_PER_BYTE < 20)        // 头部长字段 >= 最小 ip 头部长
        return;

    uint16_t check_sum16 = hdr->hdr_checksum16;         // 校验和计算不依赖大小端结果，无需 swap16()
    hdr->hdr_checksum16 = 0;
    if (check_sum16 ^ checksum16((uint16_t *)hdr, hdr->hdr_len * IP_HDR_LEN_PER_BYTE))
        return;
    hdr->hdr_checksum16 = check_sum16;

    if (memcmp(hdr->dst_ip, net_if_ip, NET_IP_LEN * sizeof(uint8_t)))
        return;

    if (swap16(hdr->total_len16) < buf->len)
        buf_remove_padding(buf, buf->len - swap16(hdr->total_len16));

    if (hdr->protocol != NET_PROTOCOL_ICMP && hdr->protocol != NET_PROTOCOL_UDP)        
        icmp_unreachable(buf, hdr->src_ip, ICMP_CODE_PROTOCOL_UNREACH);

    buf_remove_header(buf, sizeof(ip_hdr_t));
    net_in(buf, hdr->protocol, hdr->src_ip);            // 源的本层协议地址为 IP 地址 √
}

/**
 * @brief 处理一个要发送的ip分片
 * 
 * @param buf 要发送的分片
 * @param ip 目标ip地址
 * @param protocol 上层协议
 * @param id 数据包id
 * @param offset 分片offset，必须被8整除
 * @param mf 分片mf标志，是否有下一个分片
 */
void ip_fragment_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol, int id, uint16_t offset, int mf)
{
    // TO-DO
    buf_add_header(buf, sizeof(ip_hdr_t));
    ip_hdr_t *hdr = (ip_hdr_t *)buf->data;

    hdr->version          = IP_VERSION_4;
    hdr->hdr_len          = sizeof(ip_hdr_t) / IP_HDR_LEN_PER_BYTE;
    hdr->tos              = 0;

    hdr->total_len16      = swap16(buf->len);
    hdr->id16             = swap16(id);
    hdr->flags_fragment16 = swap16((mf ? IP_MORE_FRAGMENT : 0) | offset);

    hdr->ttl              = IP_DEFALUT_TTL;
    hdr->protocol         = protocol;

    memcpy(hdr->src_ip, net_if_ip, NET_IP_LEN * sizeof(uint8_t));
    memcpy(hdr->dst_ip, ip,        NET_IP_LEN * sizeof(uint8_t));

    hdr->hdr_checksum16 = 0;
    hdr->hdr_checksum16 = checksum16((uint16_t *)hdr, hdr->hdr_len * IP_HDR_LEN_PER_BYTE);

    arp_out(buf, ip);
}

/**
 * @brief 处理一个要发送的ip数据包
 * 
 * @param buf 要处理的包
 * @param ip 目标ip地址
 * @param protocol 上层协议
 */
void ip_out(buf_t *buf, uint8_t *ip, net_protocol_t protocol)
{
    // TO-DO
    const static size_t MAX_LEN =  ETHERNET_MAX_TRANSPORT_UNIT - sizeof(ip_hdr_t);

    static int  id = 0;
    int         mf = 1;
    uint16_t    offset = 0;

    buf_t       tmp_buf = *buf;             // 缓存上层传来的(大)分组 √
    size_t      cur_len = MAX_LEN;          // 当前分片的长度

    while (tmp_buf.len > 0) {
        if (tmp_buf.len <= MAX_LEN) {       // 小于 IP 协议最大负载包长时，为最后分片
            mf = 0;                         // MF = 0
            cur_len = tmp_buf.len;          // cur_len = 当前分片长度为剩余部分长度
        }
        
        buf_init(&txbuf, cur_len);
        memcpy(txbuf.data, tmp_buf.data, cur_len);
        ip_fragment_out(&txbuf, ip, protocol, id, offset, mf);

        tmp_buf.len -= cur_len;
        tmp_buf.data += cur_len;
        offset += cur_len / IP_HDR_OFFSET_PER_BYTE;
    }
    id ++;
}

/**
 * @brief 初始化ip协议
 * 
 */
void ip_init()
{
    net_add_protocol(NET_PROTOCOL_IP, ip_in);
}