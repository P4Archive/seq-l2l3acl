 #include "dpdk_lib.h"// sugar@19
 #include "actions.h"// sugar@20
 #include <unistd.h>// sugar@21
 #include <arpa/inet.h>// sugar@22
 extern void increase_counter (int counterid, int index);// sugar@23

 extern backend bg;// sugar@25

  void action_code__drop(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
 drop(pd);// sugar@209
 }// sugar@213

  void action_code__nop(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
 }// sugar@213

  void action_code_add_vlan(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
  EXTRACT_INT32_BITS(pd, field_instance_ethernet__etherType, value32)// sugar@55
 MODIFY_INT32_INT32_BITS(pd, field_instance_vlan__etherType, value32)// sugar@56
// sugar@209
  value32 = 33024;// sugar@44
 MODIFY_INT32_INT32_AUTO(pd, field_instance_ethernet__etherType, value32)// sugar@46
// sugar@209
 }// sugar@213

  void action_code_mac_learn(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
   struct type_field_list fields;// sugar@149
    fields.fields_quantity = 3;// sugar@151
    fields.field_offsets = malloc(sizeof(uint8_t*)*fields.fields_quantity);// sugar@152
    fields.field_widths = malloc(sizeof(uint8_t*)*fields.fields_quantity);// sugar@153
    fields.field_offsets[0] = (uint8_t*) (pd->headers[header_instance_standard_metadata].pointer + field_instance_byte_offset_hdr[field_instance_standard_metadata_ingress_port]);// sugar@157
    fields.field_widths[0] = field_instance_bit_width[field_instance_standard_metadata_ingress_port]*8;// sugar@158
    fields.field_offsets[1] = (uint8_t*) (pd->headers[header_instance_ethernet_].pointer + field_instance_byte_offset_hdr[field_instance_ethernet__srcAddr]);// sugar@157
    fields.field_widths[1] = field_instance_bit_width[field_instance_ethernet__srcAddr]*8;// sugar@158
    fields.field_offsets[2] = (uint8_t*) (pd->headers[header_instance_vlan_].pointer + field_instance_byte_offset_hdr[field_instance_vlan__vid]);// sugar@157
    fields.field_widths[2] = field_instance_bit_width[field_instance_vlan__vid]*8;// sugar@158

    generate_digest(bg,"mac_learn_digest",0,&fields); sleep(1);// sugar@164
// sugar@209
 }// sugar@213

  void action_code_route(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
 }// sugar@213

  void action_code_forward(packet_descriptor_t* pd, lookup_table_t** tables , struct action_forward_params parameters) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
  MODIFY_INT32_BYTEBUF(pd, field_instance_standard_metadata_egress_spec, parameters.port, 2)// sugar@71
// sugar@209
 }// sugar@213

  void action_code_broadcast(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
  value32 = 1;// sugar@44
 MODIFY_INT32_INT32_AUTO(pd, field_instance_intrinsic_metadata_mcast_grp, value32)// sugar@46
// sugar@209
 }// sugar@213

  void action_code_set_nhop(packet_descriptor_t* pd, lookup_table_t** tables , struct action_set_nhop_params parameters) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
  MODIFY_BYTEBUF_BYTEBUF(pd, field_instance_ethernet__srcAddr, parameters.smac, (field_desc(field_instance_ethernet__srcAddr)).bytewidth)// sugar@74
// sugar@209
  MODIFY_BYTEBUF_BYTEBUF(pd, field_instance_ethernet__dstAddr, parameters.dmac, (field_desc(field_instance_ethernet__dstAddr)).bytewidth)// sugar@74
// sugar@209
  MODIFY_INT32_BYTEBUF(pd, field_instance_vlan__vid, parameters.vid, 2)// sugar@71
// sugar@209
  value32 = -1;// sugar@90
 EXTRACT_INT32_AUTO(pd, field_instance_ipv4__ttl, res32)// sugar@92
 value32 += res32;// sugar@93
 MODIFY_INT32_INT32_AUTO(pd, field_instance_ipv4__ttl, value32)// sugar@94
// sugar@209
 }// sugar@213

  void action_code_strip_vlan(packet_descriptor_t* pd, lookup_table_t** tables ) {// sugar@201
     uint32_t value32, res32;// sugar@202
     (void)value32; (void)res32;// sugar@203
  EXTRACT_INT32_BITS(pd, field_instance_vlan__etherType, value32)// sugar@55
 MODIFY_INT32_INT32_BITS(pd, field_instance_ethernet__etherType, value32)// sugar@56
// sugar@209
 }// sugar@213

