#include "platform/network_policy.h"
#include <windows.h>
#include <netfw.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace ac {
std::string network_policy_notice(){
 HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<INetFwPolicy2> policy;std::string result;
 if(SUCCEEDED(CoCreateInstance(__uuidof(NetFwPolicy2),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(policy.GetAddressOf())))){
  long profiles=0;VARIANT_BOOL enabled=VARIANT_FALSE;NET_FW_ACTION inbound=NET_FW_ACTION_ALLOW;
  if(SUCCEEDED(policy->get_CurrentProfileTypes(&profiles))&&(profiles&NET_FW_PROFILE2_PUBLIC)&&SUCCEEDED(policy->get_FirewallEnabled(NET_FW_PROFILE2_PUBLIC,&enabled))&&enabled==VARIANT_TRUE&&SUCCEEDED(policy->get_DefaultInboundAction(NET_FW_PROFILE2_PUBLIC,&inbound))&&inbound==NET_FW_ACTION_BLOCK)
   result="Windows uses a Public network with inbound traffic blocked by default. Review network access before connecting another device.";
 }
 policy.Reset();if(SUCCEEDED(co))CoUninitialize();return result;
}
}
