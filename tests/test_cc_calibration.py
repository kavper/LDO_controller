"""Real ADC current and CC DAC conversion, including independent 4 A point."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def function(path,name):
 s=path.read_text();start=s.index(name+'(');start=s.rfind('\n',0,start)+1
 brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
config=(root/'Core/Inc/app_config.h').read_text()
code='#include <stdint.h>\n#include <assert.h>\n#include <math.h>\n'
for name in ['MCP3464_IOUT_GAIN_PPM','MCP3464_IOUT_ZERO_RAW','MCP3464_EXTERNAL_VREF_MV','CURRENT_SENSE_SHUNT_MILLIOHM','CURRENT_SENSE_AMPLIFIER_GAIN','CURRENT_LIMIT_AMPLIFIER_GAIN','DAC_CC_OUTPUT_GAIN_PPM','DAC_CC_OUTPUT_OFFSET_UA']:
 code+=re.search(r'^#define\s+'+name+r'\s+[^\n]+',config,re.M)[0]+'\n'
code+='#define MCP3464_SIGNED_CODES 32768LL\n'
for name in ['measurements_apply_calibration','measurements_iout_raw_to_mA']:
 code+=function(root/'Core/Src/measurements.c',name)+'\n'
code+=function(root/'Core/Src/control.c','Control_CurrentToDacRaw')
code+='''\nint main(void){
 assert(measurements_iout_raw_to_mA(0)==0);
 assert(Control_CurrentToDacRaw(0)==0);
 assert(Control_CurrentToDacRaw(UINT32_MAX)==UINT16_MAX);
 unsigned last=0;
 for(unsigned ma=0;ma<=5000;++ma){
  unsigned code=Control_CurrentToDacRaw(ma);assert(code>=last);last=code;
  if(ma>=1){
   double predicted=code*6000.0/65535.0*1.006617+0.947;
   assert(fabs(predicted-ma)<0.06);
  }
 }
'''
# All original sweep points plus the unused-in-fit 4 A validation point.
points=[(100,101,101.732),(250,253,255.228),(500,505,511.088),(1000,1007,1022),(2000,2014,2044.002),(3000,3021,3067),(4000,4029,4088.917)]
for target,measured,adc in points:
 raw=round(adc*32768/6000);oldcode=round(target*65535/6000)
 code+=f'''assert(fabs((double)measurements_iout_raw_to_mA({raw})-{measured})<=2.0);
 assert(measurements_iout_raw_to_mA(-{raw})==measurements_iout_raw_to_mA({raw}));
 {{double predicted={measured}+((int)Control_CurrentToDacRaw({target})-{oldcode})*6000.0/65535.0*1.006617;
 assert(fabs(predicted-{target})<2.0);}}
'''
code+='return 0;}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: production ADC and CC DAC, 0.1-3 A sweep + held-out 4 A, zero, monotonic 0-5 A and saturation')
