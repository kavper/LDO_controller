"""Exercise production CV DAC conversion; analogue results still need remeasurement."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'Core/Src/control.c').read_text();start=s.index('uint16_t Control_VoltageToDacRaw(');end=s.index('\nuint16_t Control_CurrentToDacRaw',start)
config=(root/'Core/Inc/app_config.h').read_text()
code='#include <stdint.h>\n#include <assert.h>\n#include <math.h>\n'
for name in ['DAC_CV_OUTPUT_GAIN_PPM','DAC_CV_OUTPUT_OFFSET_UV','MCP3464_EXTERNAL_VREF_MV','VOUT_DIFFAMP_INPUT_OHM','VOUT_DIFFAMP_FEEDBACK_OHM']:
 code+=re.search(r'^#define\s+'+name+r'\s+[^\n]+',config,re.M)[0]+'\n'
code+=s[start:end]+'''\nint main(void){
 assert(Control_VoltageToDacRaw(0)==0);
 assert(Control_VoltageToDacRaw(1)==0);
 assert(Control_VoltageToDacRaw(8)==0);
 assert(Control_VoltageToDacRaw(UINT32_MAX)==UINT16_MAX);
 unsigned last=0;
 for(unsigned mv=0;mv<=27000;++mv){
  unsigned code=Control_VoltageToDacRaw(mv);
  assert(code>=last);last=code;
  if(mv>=10){
   double predicted=(code*36000.0/65535.0)*1.0000937244953991+8.31954323615355;
   assert(fabs(predicted-mv)<0.4);
  }
 }
'''
# Preserve actual individual sweep deviations (the affine fit is approximate).
for mv,measured in [(1000,1005),(3000,3010),(5000,5011),(10000,10009),(20000,20010),(25000,25010)]:
 oldcode=round(mv*65535/36000)
 code+=f'''{{ double predicted={measured}+((int)Control_VoltageToDacRaw({mv})-{oldcode})*36000.0/65535.0*1.0000937244953991;
 assert(fabs(predicted-{mv})<4.0); }}\n'''
code+='return 0;}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: production CV DAC inverse fit, zero/OFF, saturation, monotonic 0-27 V and sweep predictions')
