"""Generate a self-contained uVision project using the known C167 tool settings."""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
# The committed project retains the inherited device/bus settings. Regenerate
# its source groups without requiring the private original checkout.
SOURCE=ROOT/'TU5JP.uvproj'
tree=ET.parse(SOURCE);project=tree.getroot();target=project.find('./Targets/Target')
target.find('TargetName').text='TU5JP Development (outputs inhibited)'
common=target.find('./TargetOption/TargetCommonOption')
common.find('OutputDirectory').text='.\\build\\uvision\\'
common.find('ListingPath').text='.\\build\\uvision\\'
common.find('OutputName').text='TU5JP'
common.find('CreateHexFile').text='1'
for prefix in ('BeforeCompile','BeforeMake','AfterMake'):
    for flag in ('RunUserProg1','RunUserProg2'):
        element=common.find(f'{prefix}/{flag}')
        if element is not None:element.text='0'
options=target.find('./TargetOption/Target166')
options.find('./C166/Optimize').text='4'
options.find('./C166/NoDppSave').text='1'  # uVision emits NODPPSAVE for value 0
options.find('./C166/VariousControls/IncludePath').text='.\\include;.\\target\\c167'
options.find('./L166/UseTargetSet').text='0'
classes = [f'{c} (0x0-0xDFFF,0x10000-0x6FFFF)' for c in ('FCODE','FCONST','HCONST','XCONST')]
classes += ['ICODE (0x0-0xDFFF)', 'NCONST (0x4000-0x7FFF)']
classes += [f'{c} (0x380000-0x383FFF)' for c in ('NDATA','NDATA0','FDATA','FDATA0','HDATA','HDATA0','XDATA','XDATA0')]
classes += [f'{c} (0xF600-0xF7FF)' for c in ('SDATA','SDATA0')]
classes += [f'{c} (0xF600-0xFDFF)' for c in ('IDATA','IDATA0')]
options.find('./L166/UserClasses').text=', '.join(classes)
for element in options.iter():
    if element.tag in ('MiscControls','UserClasses','CinitTab','iStartStopString') and element.text:
        element.text=element.text.replace('0x7FFFF','0x6FFFF').replace('0X7FFFF','0X6FFFF').replace('0x07FFFF','0x06FFFF')
for element in options.findall('./Target166Misc/OnChipMemories/Ocm2/Size'):element.text='0x70000'
groups=target.find('Groups');groups.clear()
for name,paths in [('Control core',sorted((ROOT/'src').glob('*.c'))),('C167 board',sorted((ROOT/'target/c167').glob('*.c'))+sorted((ROOT/'target/c167').glob('*.A66')))]:
    group=ET.SubElement(groups,'Group');ET.SubElement(group,'GroupName').text=name;files=ET.SubElement(group,'Files')
    for path in paths:
        entry=ET.SubElement(files,'File');ET.SubElement(entry,'FileName').text=path.name
        ET.SubElement(entry,'FileType').text='2' if path.suffix.upper()=='.A66' else '1'
        ET.SubElement(entry,'FilePath').text='.\\'+str(path.relative_to(ROOT)).replace('/','\\')
ET.indent(tree,space='  ')
tree.write(ROOT/'TU5JP.uvproj',encoding='utf-8',xml_declaration=True)
print(ROOT/'TU5JP.uvproj')
