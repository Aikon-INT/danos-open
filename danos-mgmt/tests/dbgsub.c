#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/gnmi/gnmi_proto.h"
#include "../src/gnmi/gnmi_grpc.h"
int main(){
  uint8_t req[128]; gnmi_pb_t w; gnmi_pb_init(&w,req,sizeof(req));
  size_t ls = gnmi_pb_begin_nested(&w, 1);
  {
    size_t ss = gnmi_pb_begin_nested(&w, 2);
    gnmi_path_t p; assert(gnmi_path_from_str(&p, "interfaces"));
    gnmi_encode_path(&w, 1, &p);
    gnmi_pb_end_nested(&w, ss);
  }
  gnmi_pb_put_enum(&w, 5, GNMI_SUB_MODE_ONCE);
  gnmi_pb_end_nested(&w, ls);
  printf("req len=%zu\n", w.len);
  gnmi_subscribe_request_t sr;
  bool ok = gnmi_decode_subscribe_request(req, w.len, &sr);
  printf("decode=%d mode=%d subs=%u updates_only=%d\n", ok, sr.subscribe.mode, sr.subscribe.sub_count, sr.subscribe.updates_only);
  if (sr.subscribe.sub_count) printf("sub0 elems=%u name=%s\n", sr.subscribe.subs[0].path.elem_count, sr.subscribe.subs[0].path.elems[0].name);
  return 0;
}
/* part2: exercise ONCE handler via a full serve? too complex; instead
   build the subscribe response the way the handler does and dump it */
