"""Verify actual production conversion against this board's measured VOUT points."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'Core/Src/measurements.c').read_text()
def function(name):
 start=s.index(name+'(');start=s.rfind('\n',0,start)+1
 brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
config=(root/'Core/Inc/app_config.h').read_text()
code='#include <stdint.h>\n#include <assert.h>\n#include <stdlib.h>\n'
for name in ['MCP3464_VOUT_GAIN_PPM','MCP3464_VOUT_ZERO_RAW','MCP3464_EXTERNAL_VREF_MV','VOUT_DIFFAMP_INPUT_OHM','VOUT_DIFFAMP_FEEDBACK_OHM']:
 code+=re.search(r'^#define\s+'+name+r'\s+[^\n]+',config,re.M)[0]+'\n'
code+='#define MCP3464_SIGNED_CODES 32768LL\n'+function('measurements_apply_calibration')+'\n'+function('measurements_vout_raw_to_mV')
code+='\nint main(void){\n'
# ADC codes reconstructed from pre-calibration millivolt telemetry: allow quantization.
for before,reference in [(1021,1005),(3056.004,3010),(5088.642,5011),(10166.518,10009),(20323.553,20010),(25403.640,25010)]:
 raw=round(before*32768/36000)
 code+=f'assert(abs((int)measurements_vout_raw_to_mV({raw})-{reference})<=3);\n'
code+='assert(measurements_vout_raw_to_mV(0)==0);\nassert(measurements_vout_raw_to_mV(-1)==0);\nreturn 0;}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual VOUT conversion matches six Fluke points within 3 mV, zero and negative clamp')
