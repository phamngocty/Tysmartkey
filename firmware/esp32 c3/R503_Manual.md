## Page 1

R503/R503-M22 Fingerprint Module
User Manual
Hangzhou Grow Technology Co., Ltd.
2023.09 Ver: 1.4.1
1 www.hzgrow.com

## Page 2

Preface & Declaration
Thank you foryou selection ofR503/R503-M22 FingerprintIdentification Moduleof
GROW.
TheManual istargeted for hardware & software development engineer,covering
modulefunction, hardware andsoftware interface etc.Toensure thedeveloping process
goes smoothly,it is highly recommended theManual isreadthrough carefully.
Because oftheproducts constantlyupgradedand improved,moduleand themanual
content may be changedwithout priornotice. If you want to get thelatest information,
pleasevisit ourcompany website (www.hzgrow.com).
We have been trying our best to ensure you the correctness of the Manual. However, if
you have any question or find error, feel free to contact us or the authorized agent. We
would bevery grateful.
The Manual contains proprietary information of Hangzhou Grow Technology Co., Ltd.,
which shall not be used by ordisclosed to third parties without the permission of GROW,
norfor any reproduction and alteration of information without any associated warranties,
conditions,limitations, ornotices.
No responsibility or liability is assumed by GROW for the application or use, nor for
any infringements of patents or other intellectual property rights of third parties that may
result from its use.
www.hzgrow.com
I www.hzgrow.com

## Page 3

Revised Version
Version
Date ReviseContent Modifier
Number
1. LED colors increased from three to seven,
and the instruction formats were downward
compatible.
2. Added 0x31 automatic registration template:
the upper computer can automatically collect 6
images by sending only one command, and then
generatetemplatesforsaving.
V1.2 2021.10 3. Added 0x32 automatic fingerprint GrowTech
verification: the upper computer can only send
one command to realize image collection,
generate features, search and compare the
fingerprint database, and return the comparison
results
4. Addedtemplateuploadprocedureflow
5. Addedtemplatedownloadprocess
1. Added Power Supply Requirements,Ripple
noise
V1.2.1 2022.03 GrowTech
2. Update Command: The note of UpChar and
DownChar
V1.2.2 2022.10 1. AddedBuffercontents GrowTech
1. Added Basic communication flow and
generalinstructioncommunicationflow
2. Updated Acknowledge package format of
ReadproductinformationCommand(0x3C)
V1.3 2023.06 GrowTech
3. Updated Automatic fingerprint verification
Command(0x32)
4. Updated Automatic registration template
Command(0x31)
V1.4 2023.07 1. AddedR503-M22SizeVersion GrowTech
1. UpdatedSecuritylevel,Checksum
V1.4.1 2023.09 2. Addedexampleinstructionfor GrowTech
AuraLedConfig,AutoEnroll,AutoIdentify
II www.hzgrow.com

## Page 4

Catalog
I Introduction...................................................................................................................................-1-
OperationPrinciple.....................................................................................................................-1-
II HardwareInterface.......................................................................................................................-2-
ExteriorInterface........................................................................................................................-2-
R503Info............................................................................................................................-2-
R503-M22Info...................................................................................................................-2-
SerialCommunication................................................................................................................-3-
HardwareConnection.................................................................................................................-3-
Serialcommunicationprotocol...................................................................................................-3-
Power-ondelaytime...................................................................................................................-4-
PowerSupplyRequirements......................................................................................................-4-
Ripplenoise................................................................................................................................-4-
III SystemResources.......................................................................................................................-5-
Notepad.......................................................................................................................................-5-
Buffer..........................................................................................................................................-5-
FingerprintLibrary.....................................................................................................................-5-
SystemConfigurationParameters..............................................................................................-5-
Baudratecontrol(ParameterNumber:4)..........................................................................-6-
SecurityLevel(ParameterNumber:5)..............................................................................-6-
Datapackagelength(ParameterNumber:6).....................................................................-6-
Systemstatusregister.................................................................................................................-6-
Modulepassword........................................................................................................................-6-
Moduleaddress...........................................................................................................................-6-
Randomnumbergenerator.........................................................................................................-7-
Featuresandtemplates................................................................................................................-7-
IV CommunicationProtocol............................................................................................................-8-
Datapackageformat...................................................................................................................-8-
InstructionTable.........................................................................................................................-9-
Checkandacknowledgementofdatapackage...........................................................................-9-
V ModuleInstructionSystem........................................................................................................-11-
System-relatedinstructions.......................................................................................................-11-
Verifypassword VfyPwd...............................................................................................-11-
Setpassword SetPwd...................................................................................................-11-
SetModuleaddress SetAdder..............................................................-12-
Setmodulesystem’sbasicparameter SetSysPara.......................................................-12-
ReadsystemParameter ReadSysPara.......................................................-13-
Readvalidtemplatenumber TempleteNum..............................................................-13-
Readfingerprinttemplateindextable ReadIndexTable(0x1F)..............................-14-
Getthealgorithmlibraryversion GetAlgVer(0x39)..............................................-15-
Getthefirmwareversion GetFwVer(0x3A).............................................................-15-
Readproductinformation ReadProdInfo(0x3C)......................................................-16-
Fingerprint-processinginstructions..........................................................................................-17-
Tocollectfingerimage GetImg.................................................................................-17-
Touploadimage UpImage......................................................................................-17-
III www.hzgrow.com

## Page 5

Todownloadtheimage DownImage.......................................................................-18-
Togeneratecharacterfilefromimage GenChar.......................................................-19-
Togeneratetemplate RegModel............................................................................-19-
Touploadtemplate UpChar.......................................................................................-20-
Todownloadtemplate DownChar...............................................................................-20-
Tostoretemplate Store............................................................................................-21-
ToreadtemplatefromFlashlibrary LoadChar.......................................................-22-
Todeletetemplate DeletChar..............................................................................-22-
Toemptyfingerlibrary Empty..............................................................................-23-
Tocarryoutprecisematchingoftwofingertemplates Match.............................-23-
Tosearchfingerlibrary Search...........................................................................-24-
Fingerprintimagecollectionextensioncommand GetImageEx(0x28)....................-24-
Cancelinstruction Cancel(0x30).........................................................................-25-
HandShake HandShake(0x40).........................................................................-26-
CheckSensor CheckSensor (0x36).....................................................................-26-
Softreset SoftRst(0x3D)..................................................................................-27-
Auracontrol AuraLedConfig (0x35).............................................................-27-
Automaticregistrationtemplate AutoEnroll (0x31).......................................-28-
Automaticfingerprintverification AutoIdentify (0x32).................................-31-
Otherinstructions......................................................................................................................-32-
Togeneratearandomcode GetRandomCode.......................................-32-
Toreadinformationpage ReadInfPage.......................................................................-33-
Towritenotepad WriteNotepad..............................................................................-33-
Toreadnotepad ReadNotepad................................................................................-34-
Ⅵ OperationProcess.....................................................................................................................-35-
6.1Basiccommunicationflow.................................................................................................-35-
6.1.1ProcessoftheUARTcommandpackage................................................................-35-
6.1.2UARTPacketSendingProcess...............................................................................-36-
6.1.3UARTpacketreceivingprocess..............................................................................-37-
6.2Generalinstructioncommunicationflow...........................................................................-38-
6.2.1Generalinstructionregisterfingerprintprocess......................................................-38-
6.2.2Generalinstructionverityfingerprintprocess.........................................................-39-
6.2.3ReadaspecifiedtemplateuploadtoFlashFingerprintDatabase...........................-40-
6.3AutomaticRegisterFingerprint..........................................................................................-41-
6.4AutomaticFingerprintVerification(Search).......................................................................-42-
6.5Lowpowerstandby.............................................................................................................-43-
Ⅶ ReferenceCircuit.....................................................................................................................-44-
IV www.hzgrow.com

## Page 6

I Introduction
Power DC3.3V Interface UART(3.3VTTLlogical
level)
Workingcurrent 20mA MatchingMode 1:1and1:N
(Fingerprint MatchingTime 1:N<10ms/Fingerprint
acquisition)
Standbycurrent Typicaltouchstandby Characteristicvalue 512bytes
(fingerdetection) voltage:3.3V size
Averagecurrent:2uA
Baudrate (9600*N)bps, Templatesize 1536bytes
N=1～6(defaultN=6）
Imageacquiringtime <0.2s Imageresolution 508dpi
SensingArray 192*192pixel DetectionArea Diameter15mm
Storagecapacity 200 Securitylevel 3(1,2,3,4,5(highest))
FAR <0.001% FRR <1%
Generatefeature <500ms Startingtime ≤50ms
pointtime
Working Temp:-20℃-+60℃ Storage Temp:-40℃-+75℃
environment RH:10%-85% environment RH:<85%
Operation Principle
Fingerprint processing includes two parts: fingerprint enrollment and fingerprint matching (the
matchingcanbe1:1or1:N).
When enrolling, user needs to enter the finger two times. The system will process the two time
finger images, generate a template of the finger basedon processing results and store the template.
When matching, user enters the finger through optical sensor and system will generate a template
of the finger and compare it with templates of the finger library. For 1:1 matching, system will
compare the live finger with specific template designated in the Module; for 1:N matching, or
searching, system will search the whole finger library for the matching finger. In both
circumstances,systemwillreturnthematchingresult,successorfailure.
-1- www.hzgrow.com

## Page 7

II Hardware Interface
Exterior Interface
R503 Info
Connector:SH1.0--6P Thread:M25
Productexternaldiameter:28mm Innerdiameter:25mm Height:19mm
Enclosurematerial:ZincAlloy
(Standard height is 19mm, also have 15mm and 32mm height,or need black aluminium alloy
enclosure,plscontactsales,supportcustomized)
R503-M22 Info
Connector:SH1.0--6P Thread:M22
Enclosurematerial:ZincAlloy
Productexternaldiameter:25mm Innerdiameter:22mm Height:15mm
-2- www.hzgrow.com

## Page 8

Serial Communication
Connector:SH1.0--6P
Pin Name Description Pic
1 PowerSupply DC3.3V
Signalground.
2 GND
Connectedtopowerground.
3 TXD Dataoutput.TTLlogicallevel
4 RXD Datainput.TTLlogicallevel
Finger Detection Signal. Standby-high level,
5 WAKEUP
havefinger-outputlowlevel. Note:
The line orderhas nothing
6 3.3VT Touchinductionpowersupply,DC3—5V
todowithcolor.
Hardware Connection
The RX of the module is connected with theTX of the upper computer, and theTX of the module
is connectedwith theRX ofthe uppercomputer.The IRQ signal canbeconnected with themiddle
fractureorIOportoftheuppercomputer.
To reduce the system standby power consumption,when the upper computer needs to use the
fingerprint module,then power on the main power supply of the fingerprint module. At this time,
thefingerprintmoduleispoweredon,andcompletethecorrespondinginstructionssentbytheupper
computer.When the upper computer does not need to use the fingerprint module, disconnect the
fingerprintmodulefromthemainpowersupply.
Whentheuppercomputerisinstandbymode,inordertokeepthefingertouchdetection,thetouch
power supply needs to be powered all the time. The working voltage of the touch power supply is
3V~5V, and the average current of the touch power supply is about 2uA. When there is no finger
touch,thedefaulttouchsensing signal outputs high level;When afinger touches,the defaulttouch
sensing signal outputs low level. After detecting the touch sensing signal, the upper computer
suppliespowertothefingerprintmoduleandthefingerprintmodulestartstowork.
Themaximumresponsetimeofthetouchfunctionisabout120mS@vt=3.3V.Whenthemoduleis
not touched, the recalibration period is about 4.0sec; the touch signal output is CMOS output, and
theoutputvoltageisroughlythesameastheinputvoltage.
Serial communication protocol
Themodeissemiduplexasychronismserialcommunication.Andthedefaultbaudrateis57600bps.
Usermaysetthebaudratein9600～115200bps。
Transferring frame format is 10 bit: the low-level starting bit, 8-bit data with the LSB first, and an
endingbit.Thereisnocheckbit.
-3- www.hzgrow.com

## Page 9

Power-on delay time
At power on, it takes about 50ms for initialization. During this period, the module can’t accept
commands for upper computer.After completing the initialization, the module will immediately
send a byte (0x55) to the upper computer, indicating that the module can work normally and
receiveinstructionsfromtheuppercomputer.
Power Supply Requirements
The power supply is DC +3.3V. The power input is allowed only after the R503/R503-M22 is
properlyconnected.
Electrical components of the R503/R503-M22 may be damaged if you insert or remove the cable
(with the electric plug) when the cable is live. Ensure that the power supply is switched off when
youinsertorremovethecable.
The R503/R503-M22 may not work properly due to poor power connections, short power off/on
intervals, or excessive voltage drop pulses. So pls keep the power is stable. After the power is
turnedoff,thepowermustbeturnedonatleasttwosecondslater.
Ripple noise
Since the power input of R503/R503-M22 is directly supplied to the image sensor and decoding
chip.
Toensurestableoperation,plsuselowripplenoisepowerinput.
Itisrecommendedthattheripplenoisenotexceed50mV(peak-to-peak).
-4- www.hzgrow.com

## Page 10

III System Resources
To address demands of different customer, Module system provides abundant resources at user’s
use.
Notepad
The system sets aside a 512-bytes memory (16 pages* 32 bytes) for user’s notepad, where data
requiring power-off protection can be stored. The host can access the page by instructions of
PS_WriteNotepadandPS_ReadNotepad.
Note:whenwrite ononepage ofthepad,the entire32bytes willbewrittenin whollycoveringthe
originalcontents.
The user can run the module address or random number command to configure the unique
matchingbetweenthemoduleandthesystem.Thatis,thesystemidentifiesonlytheuniquemodule.
Ifamoduleofthesametypeisreplaced,thesystemcannotaccessthesystem.
Buffer
ThemoduleRAMresourcesareasfollows:
AnImageBuffer:ImageBuffer
6featurebuffers:CharBuffer[1:6]
Allbuffercontentsarenotsavedwithoutpower.
The user can read and write any buffer by instruction. CharBuffer can be used to store normal
featurefilesorstoretemplatefeaturefiles.
When uploading or downloading images through the UART port, only the high four bits of pixel
bytes are usedto speedup the transmission, thatis, usegray level 16,two pixels are combined into one
byte.(Thehighfourbitsareapixel,thelowfourbitsareapixelinthenextadjacentcolumnofthesame
row,thatis,twopixelsarecombinedintoonebyteandtransmitted)
Since the image has 16 gray levels, when it is uploaded to PC for display (corresponding to BMP
format),thegraylevelshouldbeextended(256graylevels,thatis,8bitbitmapformat).
Fingerprint Library
System sets aside a certain space within Flash for fingerprint template storage, that’s fingerprint
library. The contents of the fingerprint database are protected by power-off, and the serial number
ofthefingerprintdatabasestartsfrom0.
Capacity of the library changes with the capacity of Flash, system will recognize the latter
automatically.Fingerprint template’s storage in Flash is in sequential order.Assume the fingerprint
capacity N, then the serial number of template in library is 0, 1, 2, 3 … N. User can only access
librarybytemplatenumber.
System Configuration Parameters
The system allows the user to individually modify a specified parameter value (by parameter serial
number) by command. Refer to SetSysPara. After the upper computer sets the system parameter
instructions, the system must be powered on again so that the module can work according to the new
-5- www.hzgrow.com

## Page 11

configuration.
Baud rate control (Parameter Number: 4)
TheParametercontrolstheUARTcommunicationspeedoftheModule.ItsvalueisanintegerN,
N=[1/2/4/6/12].Correspondingbaudrateis9600*Nbps。
Security Level (Parameter Number: 5)
The Parameter controls the matching threshold value of fingerprint searching and matching.
Security level is divided into 5 grades, and corresponding value is 1, 2, 3, 4, 5.At level 1, FAR is
thehighestandFRRisthelowest;howeveratlevel5,FARisthelowestandFRRisthehighest.
Data package length (Parameter Number: 6)
The parameter decides the max length of the transferring data package when communicating with
upper computer. Its value is 0, 1, 2, 3, corresponding to 32 bytes, 64 bytes, 128 bytes, 256 bytes
respectively.
System status register
SystemstatusregisterindicatesthecurrentoperationstatusoftheModule.Itslengthis1word,and
canbereadviainstructionReadSysPara.Definitionoftheregisterisasfollows:
BitNum 15 4 3 2 1 0
Description Reserved ImgBufStat PWD Pass Busy
Note:
Busy：1bit.1:systemisexecutingcommands;0:systemisfree;
Pass：1bit.1:findthematchingfinger;0:wrongfinger;
PWD：1bit.1:Verifieddevice’shandshakingpassword.
ImgBufStat：1bit.1:imagebuffercontainsvalidimage.
Module password
The default password of the module is 0x00000000. If the default password is modified, after the
module is powered on,the first instruction of the upper computer to communicate with the module
must be verify password. Only after the password verification is passed, the module will enter the
normalworkingstateandreceiveotherinstructions.
The new modified password is stored in Flash and remains at power off.(the modified password
cannot be obtained through the communication instruction. If forgotten by mistake, the module
cannotcommunicate,pleaseusewithcaution)
RefertoinstructionSetPwdandVfyPwd.
Module address
Each module has an identifying address. When communicating with upper computer, each
instruction/data is transferred in data package form, which contains the address item. Module
system only responds to data package whose address item value is the same with its identifying
address.
-6- www.hzgrow.com

## Page 12

The address length is 4 bytes, and its default factory value is 0xFFFFFFFF. User may modify the
addressviainstructionSetAddr.Thenewmodifiedaddressremainsatpoweroff.
Random number generator
Module integrates a hardware 32-bit random number generator (RNG) (without seed). Via
instructionGetRandomCode,systemwillgeneratearandomnumberanduploadit.
Features and templates
The chip has one image buffer and six feature file buffers,all buffer contents are not saved after
powerfailure.
Atemplatecanbecomposedof2-6featurefiles.Themorefeaturefilesinthesynthesistemplate,
thebetterthequalityofthefingerprinttemplate.
Itisrecommendedtotakeatleastfourtemplatestosynthesizefeatures.
-7- www.hzgrow.com

## Page 13

IV Communication Protocol
The protocol defines the data exchanging format when R503/R503-M22 series communicates with
uppercomputer.TheprotocolandinstructionsetsapplesforbothUARTcommunicationmode.
Baudrate57600,databit8,stopbit1,paritybitnone.
Data package format
When communicating, the transferring and receiving of command/data/result are all wrapped in
data package format. For multi-bytes, the high byte precedes the low byte (for example, a 2 bytes
0006indicates0006,not0600).
Data packageformat
Header Adder Package Package Packagecontent Checksum
identifier length (instruction/data/Parameter）
DefinitionofData package
Name Symbol Length Description
Header Start 2bytes Fixedvalueof0xEF01;Highbytetransferredfirst.
Default value is 0xFFFFFFFF, which can be modified by
Adder ADDER 4bytes command.High byte transferredfirstandatwrongaddervalue,
modulewillrejecttotransfer.
01H Commandpacket;
Data packet; Data packet shall not appear alone in
02H executing process, must follow command packet or
Package
PID 1byte acknowledgepacket.
identifier
07H Acknowledgepacket;
08H EndofDatapacket.
Refers to the length of package content (command packets and
Package
LENGTH 2bytes data packets) plus the length of Checksum( 2 bytes). Unit is
length
byte.Maxlengthis256bytes.Andhighbyteistransferredfirst.
It can be commands, data, command’s parameters,
Package
DATA － acknowledge result, etc. (fingerprint character value, template
contents
arealldeemedasdata);
The arithmetic sum of package identifier, package length and
Checksum SUM 2bytes all package contents. Overflowing bits are omitted. high byte is
transferredfirst.
-8- www.hzgrow.com

## Page 14

Instruction Table
Code Identifier Description Code Identifier Description
01H GenImg Collectfingerimage 12H SetPwd Tosetpassword
02H Genchar To generate character file 13H VfyPwd Toverifypassword
fromimage
03H Match Carry out precise 14H GetRandomCo togetrandomcode
matching of two de
templates;
04H Search Searchthefingerlibrary 15H SetAdder Tosetdeviceaddress
05H RegModel To combinecharacterfiles 16H ReadInfPage Readinformationpage
andgeneratetemplate
06H Store Tostoretemplate; 18H WriteNotepad towritenotepad
07H LoadChar toread/loadtemplate 19H ReadNotepad Toreadnotepad
08H UpChar touploadtemplate 1DH TempleteNum To read finger template
numbers
09H DownChar todownloadtemplate 1FH ReadIndexTabl Read-fingerprint
e templateindextable
0AH UpImage Touploadimage 0x28 GetImageEx Fingerprint image collection
extensioncommand
0BH DownImage Todownloadimage 0x30 Cancel Cancelinstruction
0CH DeletChar todeletetemplates 0x40 HandShake HandShake
0DH Empty toemptythelibrary 0x36 Check CheckSensor
Sensor
0EH SetSysPara TosetsystemParameter 0x39 GetAlgVer Getthealgorithmlibrary
version
0FH ReadSysPar ToreadsystemParameter 0x3C ReadProdInfo Read product
a information
0x3A GetFwVer Getthefirmwareversion 0x35 Auracontrol AuraLedConfig
0x3D SoftRst Softreset 0x32 AutoIdentify Automatic fingerprint
verification
0x31 AutoEnroll Automatic registration
template
Check and acknowledgement of data package
Note: Commands shall only be sent from upper computer to the Module, and the Module
acknowledgesthecommands.
Uponreceiptofcommands,Modulewillreportthecommandsexecutionstatusandresultstoupper
computer through acknowledge packet. Acknowledge packet has parameters and may also have
following data packet. Upper computer can’t ascertain Module’s package receiving status or
command execution results unless through acknowledge packet sent from Module. Acknowledge
packetincludes1byteconfirmationcodeandmaybealsothereturnedparameter.
Confirmationcode’sdefinitionis:
00h:commandexecutioncomplete;
01h:errorwhenreceivingdatapackage;
-9- www.hzgrow.com

## Page 15

02h:nofingeronthesensor;
03h:failtoenrollthefinger;
06h:failtogeneratecharacterfileduetotheover-disorderlyfingerprintimage;
07h: fail to generate character file due to lackness of character point or over-smallness of
fingerprintimage
08h:fingerdoesn’tmatch;
09h:failtofindthematchingfinger;
0Ah:failtocombinethecharacterfiles;
0Bh:addressingPageIDisbeyondthefingerlibrary;
0Ch:errorwhenreadingtemplatefromlibraryorthetemplateisinvalid;
0Dh:errorwhenuploadingtemplate;
0Eh:Modulecan’treceivethefollowingdatapackages.
0Fh:errorwhenuploadingimage;
10h:failtodeletethetemplate;
11h:failtoclearfingerlibrary;
13h:wrongpassword!
15h:failtogeneratetheimageforthelacknessofvalidprimaryimage;
18h:errorwhenwritingflash;
19h:Nodefinitionerror;
20h:theaddresscodeisincorrect;
21h:passwordmustbeverified;
22h:fingerprinttemplateisempty;
24h:fingerprintlibraryisempty;
26h:timeout
27h:fingerprintsalreadyexist;
29h:sensorhardwareerror;
1Ah:invalidregisternumber;
1Bh:incorrectconfigurationofregister;
1Ch:wrongnotepadpagenumber;
1Dh:failtooperatethecommunicationport;
1Fh:fingerprintlibraryisfull;
FCh:unsupportedcommand;
FDh:hardwareerror;
FEh:commandexecutionfailure;
others:systemreserved;
-10- www.hzgrow.com

## Page 16

V Module Instruction System
System-related instructions
Verify password VfyPwd
Description:VerifyModule’shandshakingpassword.
InputParameter:PassWord(4bytes)
ReturnParameter:Confirmationcode(1byte)
Instructioncode:13H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 4bytes 2bytes
Header Module Package Package Instruction Password Checksum
address identifier length code
0xEF01 xxxx 01H 0007H 13H PassWord sum
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Confirmation Checksum
identifier Length code
0xEF01 xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:Correctpassword;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=13H:Wrongpassword;
Set password SetPwd
Description:SetModule’shandshakingpassword.
InputParameter:PassWord(4bytes)
ReturnParameter:Confirmationcode(1byte)
Instructioncode:12H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 4bytes 2bytes
Header Module Package Package Instruction Password Checksum
address identifier length code
0xEF01 xxxx 01H 0007H 12H PassWord sum
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Confirmation Checksum
identifier Length code
0xEF01 xxxx 07H 0003H xxH sum
Note: Confirmationcode=00H:passwordsettingcomplete;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=21H:havetoverifypassword
Confirmationcode=18H:errorwhenwriteFLASH
-11- www.hzgrow.com

## Page 17

Set Module address SetAdder
Description:SetModuleaddress.
InputParameter:Addr
ReturnParameter:Confirmationcode(1byte)
Instructioncode:15H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 4bytes 2bytes
Header Original Package Package Instruction NewModule Checksum
Moduleaddress identifier length code address
0xEF01 xxxx 01H 0007H 15H Addr sum
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header NewModule Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 07H 0003H xxH Sum
Note:Confirmationcode=00H:addresssettingcomplete;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=18H:errorwhenwriteFLASH
Set module system’s basic parameter SetSysPara
Description:Operationparametersettings.
InputParameter:Parameternumber+Contents
ReturnParameter:Confirmationcode(1byte)
Instructioncode:0eH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 1byte 2bytes
Header Module Package Package Instruction Parameter Contents Checksum
address identifier length code number
0xEF01 Xxxx 01H 0005H 0eH 4/5/6 xx sum
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH Sum
Note: Confirmationcode=00H:parametersettingcomplete;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=1aH:wrongregisternumber;
Confirmationcode=18H:errorwhenwriteFLASH
-12- www.hzgrow.com

## Page 18

Name Parameternumber Content
Datarange:1,2/4/6/12,
Baudrate
4 indicatesthatbaudrateis9600*Nbps
Securitylevel 5 Datarange:1,2,3,4,5
Datarange:0,1,2,3thecorrespondinglengths(bytes)
Packetcontentlength
6 areasfollows:32,64,128,256
Read system Parameter ReadSysPara
Description:ReadModule’sstatusregisterandsystembasicconfigurationparameters;
InputParameter：none
ReturnParameter：Confirmationcode(1byte)+basicparameter（16bytes）
Instructioncode:0fH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instruction Checksum
address identifier code
0xEF01 Xxxx 01H 0003H 0fH 0013H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 16bytes 2bytes
Header Module Package Package Confirmation Basicparameter Checksum
address identifier length code list
0xEF01 xxxx 07H 0013H xxH Seefollowing sum
table
Note:Confirmationcode=00H:readcomplete;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=18H:errorwhenwriteFLASH
Name Description Offset(word) Size(word)
Statusregister Contentsofsystemstatusregister 0 2
Systemidentifiercode Fixedvalue:0x0000 1 2
Fingerlibrarysize Fingerlibrarysize 2 2
Securitylevel Securitylevel(1,2,3,4,5) 3 2
Deviceaddress 32-bitdeviceaddress 4 4
Datapacketsize Sizecode(0,1,2,3) 6 2
Baudsettings N(baud=9600*Nbps) 7 2
Read valid template number TempleteNum
Description:readthecurrentvalidtemplatenumberoftheModule
InputParameter:none
ReturnParameter: Confirmationcode(1byte)，templatenumber:N
Instructioncode:1dH
Command(orinstruction)packageformat:
-13- www.hzgrow.com

## Page 19

2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Instruction Checksum
identifier length code
0xEF01 xxxx 01H 0003H 1dH 0021H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes 2bytes
Header Module Package Package Confirmation Template Checksum
address identifier length code number
0xEF01 xxxx 07H 0005H xxH Num sum
Note:Confirmationcode=0x00:readsuccess;
Confirmationcode=0x01:errorwhenreceivingpackage;
Read fingerprint template index table ReadIndexTable (0x1F)
Description: Read the fingerprint template index table of the module, read the index table of the
fingerprinttemplateupto256atatime(32bytes)
InputParameter:Indexpage
ReturnParameter: Confirmationcode+Fingerprinttemplateindextable
Instructioncode:0x1F
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes
Header Module Package Package Instruction Indexpage Checksum
address identifier length code
0xEF01 xxxx 0x01 0x0004 0x1F 0/1/2/3 Sum
Indextablesarereadperpage,256templatesperpage
Indexpage0meanstoread0~255fingerprinttemplateindextable
Indexpage1meanstoread256~511fingerprinttemplateindextable
Indexpage2meanstoread512~767fingerprinttemplateindextable
Indexpage3meanstoread768~1023fingerprinttemplateindextable
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 32bytes 2bytes
Header Module Package Package Confirmation Indexpage Check-
address identifier length code sum
0xEF01 xxxx 0x07 0x0023 X Seethetablebelow sum
Note:Confirmationcode=0x00:readcomplete;
Confirmationcode=0x01:errorwhenreceivingpackage;
Index table structure: every 8 bits is a group, and each group is output starting from the high
position.
transport Theoutputissequentialfromlowbytetohighbyte,andeachbytestartsatahighbyte.
order
T[0] Templatenumber 7 6 5 4 3 2 1 0
Indextabledata 0/1 0/1 0/1 0/1 0/1 0/1 0/1 0/1
T[1] Templatenumber 15 14 13 12 11 10 9 8
-14- www.hzgrow.com

## Page 20

Indextabledata 0/1 0/1 0/1 0/1 0/1 0/1 0/1 0/1
… …
T[31] Templatenumber 255 254 253 252 251 250 249 248
Indextabledata 0/1 0/1 0/1 0/1 0/1 0/1 0/1 0/1
Data "0" in the index table means that there is no valid template in the corresponding position;"1"
meansthatthereisavalidtemplateinthecorrespondingposition.
Get the algorithm library version GetAlgVer (0x39)
Description:Getthealgorithmlibraryversion
InputParameter:none
ReturnParameter:Confirmationcode+AlgVer(algorithmlibraryversionstring)
Instructioncode:0x39
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x39 003DH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 32bytes 2bytes
Header Module Package Package Confirmation Random Checksum
address identifier length code number
0xEF01 xxxx 0x07 0x0023 X AlgVer sum
Note1:Confirmationcode=0x00:success;
Confirmationcode=0x01:errorwhenreceivingpackage;
Get the firmware version GetFwVer (0x3A)
Description:Getthefirmwareversion
InputParameter:none
ReturnParameter:Confirmationcode+FwVer(Firmwareversionstring)
Instructioncode:0x3A
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x3A 003EH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 32bytes 2bytes
Header Module Package Package Confirmation Random Checksum
address identifier length code number
0xEF01 xxxx 0x07 0x0023 X FwVer sum
Note1:Confirmationcode=0x00:success;
Confirmationcode=0x01:errorwhenreceivingpackage;
-15- www.hzgrow.com

## Page 21

Read product information ReadProdInfo (0x3C)
Description:Readproductinformation
InputParameter:none
ReturnParameter:Confirmationcode+ProdInfo(productinformation)
Instructioncode:0x3C
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x3C 0040H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 50bytes 2bytes
Header Module Package Package Confirmation Product Checksu
address identifier length code information m
0xEF01 xxxx 0x07 0x0031 X ProdInfo sum
Note1:Confirmationcode=0x00:success;
Confirmationcode=0x01:errorwhenreceivingpackage;
Product information: store in the following order.For Numbers, the high byte comes first.For a
string,theinsufficientpartis0x00.
Code Bytes Meaning
PARAM_FPM_MODEL 16 moduletype,ASCII
PARAM_BN 4 Modulebatchnumber,ASCII
PARAM_SN 8 Moduleserialnumber,ASCII
For the hardware version, the first byte represents the
PARAM_HW_VER 2 main version and the second byte represents the
sub-version
PARAM_FPS_MODEL 8 Sensortype,ASCII
PARAM_FPS_WIDTH 2 Sensorimagewidth
PARAM_FPS_HEIGHT 2 Sensorimageheight
PARAM_TMPL_SIZE 2 Templatesize
PARAM_TMPL_TOTAL 2 Fingerprintdatabasesize
Other 4 SystemReserved
-16- www.hzgrow.com

## Page 22

Fingerprint-processing instructions
To collect finger image GetImg
Description: detecting finger and store the detected finger image in ImageBuffer while
returning successfully confirmation code; If there is no finger, returned confirmation code
wouldbe“can’tdetectfinger”.
ThedifferencebetweenGetImageExandGetImageinstruction:
GetImage: When the image quality is poor, return confirmation code 0x00 (the image is
successfullycaptured).
GetImageEx:When image quality is poor,return confirmation code 0x07 (image quality
istoopoor).
InputParameter:none
ReturnParameter:Confirmationcode(1byte)
Instructioncode:01H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instruction Checksum
address identifier code
0xEF01 Xxxx 01H 0003H 01H 0005H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH Sum
Note:Confirmationcode=00H:fingercollectionsuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=02H:can’tdetectfinger;
Confirmationcode=03H:failtocollectfinger;
To upload image UpImage
Description:touploadtheimageinImg_Buffertouppercomputer.
InputParameter:none
ReturnParameter:Confirmationcode(1byte)
Instructioncode:0aH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Packagelength Instructioncode Checksum
identifier
0xEF01 Xxxx 01H 0003H 0aH 000eH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH sum
-17- www.hzgrow.com

## Page 23

Note1： Confirmationcode=00H:readytotransferthefollowingdatapacket;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0fH:failtotransferthefollowingdatapacket;
2. Theuppercomputersendsthecommandpacket,themodulesendstheacknowledge
packetfirst,andthensendsseveraldatapacket.
3. Packet Bytes N is determined by Packet Length. The value is 128 Bytes before
delivery.
Datapackageformat:
2bytes 4bytes 1byte 2bytes Nbytes 2bytes
Header Module Packageidentifier Package Package Checksum
address length content
0xEF01 xxxx 0x02-have N+2 Imagedata sum
followingpacket
0x08-endpacket
To download the image DownImage
Description:todownloadimagefromuppercomputertoImg_Buffer.
InputParameter:none
ReturnParameter:Confirmationcode(1byte)
Instructioncode:0bH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Instruction Checksum
identifier length code
0xEF01 Xxxx 01H 0003H 0bH 000fH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH sum
Note:1：Confirmationcode=00H:readytotransferthefollowingdatapacket;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0eH:failtotransferthefollowingdatapacket;
2.The upper computer sends the command packet, the module sends the acknowledge
packetfirst,andthensendsseveraldatapacket.
3.PacketBytesNisdeterminedbyPacketLength.Thevalueis128Bytesbeforedelivery.
Datapackageformat:
2bytes 4bytes 1byte 2bytes Nbytes 2bytes
Header Module Packageidentifier Package Package Checksum
address length content
0xEF01 xxxx 0x02-have N+2 Imagedata sum
followingpacket
0x08-endpacket
-18- www.hzgrow.com

## Page 24

To generate character file from image GenChar
Description:togeneratecharacterfilefromtheoriginalfingerimageinImageBuffer
InputParameter:BufferID(characterfilebuffernumber)
ReturnParameter:Confirmationcode(1byte)
Instructioncode:02H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes
Header Module Package Package Instruction Buffer Checksum
address identifier length code number
0xEF01 xxxx 01H 0004H 02H CharBuffer sum
ID
CharBufferID:Characterbuffernumber,range1-6.
The R300-A module requires a minimum of four and a maximum of six fingerprint
featuresforthegeneratetemplate.
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Confirmation Checksum
address identifier code
0xEF01 xxxx 07H 0003H XxH sum
Note:Confirmationcode=00H:generatecharacterfilecomplete;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmation code=06H: fail to generate character file due to the over-disorderly
fingerprintimage;
Confirmation code=07H: fail to generate character file due to lackness of character
pointorover-smallnessoffingerprintimage;
Confirmation code=15H: fail to generate the image for the lackness of valid primary
image;
To generate template RegModel
Description:TocombineinformationofcharacterfilesfromCharBuffer1andCharBuffer2and
generateatemplatewhichisstoredbackinbothCharBuffer1andCharBuffer2.
InputParameter：none
ReturnParameter：Confirmationcode(1byte)
Instructioncode:05H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Instruction Checksum
address identifier length code
0xEF01 xxxx 01H 0003H 05H 0009H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
-19- www.hzgrow.com

## Page 25

0xEF01 xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:operationsuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0aH:failtocombinethecharacterfiles.That’s,thecharacterfiles
don’tbelongtoonefinger.
To upload template UpChar
Description:UploadthedatainthetemplatebufferModelBuffertotheuppercomputer.
InputParameter:CharBufferID
ReturnParameter:Confirmationcode(1byte)
Instructioncode:08H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes
Header Module Package Package Instruction Buffer Checksum
address identifier length code number
0xEF01 xxxx 01H 0004H 08H CharBuffer sum
ID
Note: This command don’t need to use the CharBufferID, so the CharBufferID can be
anyvaluebetween1and6.
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 07H 0003H xxH sum
Note1:Confirmationcode=00H:readytotransferthefollowingdatapacket;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0dH:errorwhenuploadingtemplate;
Confirmationcode=0fH:cannotreceivethefollowingdatapacket
4. Theuppercomputersendsthecommandpacket,themodulesendstheacknowledge
packetfirst,andthensendsseveraldatapacket.
5. Packet Bytes N is determined by Packet Length. The value is 128 Bytes before
delivery.
6:Theinstructiondoesn’taffectbuffercontents.
Datapackageformat:
2bytes 4bytes 1byte 2bytes Nbytes 2bytes
Header Module Packageidentifier Package Package Checksum
address length content
0xEF01 xxxx 0x02-have N+2 Template sum
followingpacket data
0x08-endpacket
To download template DownChar
Description:uppercomputerdownloadtemplatetomodulebuffer
InputParameter:CharBufferID(Buffernumber)
-20- www.hzgrow.com

## Page 26

ReturnParameter:Confirmationcode(1byte)
Instructioncode:09H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes
Header Module Package Package Instruction Buffernumber Checksum
address identifier length code
0xEF01 xxxx 01H 0004H 09H CharBufferID sum
Note: This command don’t need to use the CharBufferID, so the CharBufferID can be
anyvaluebetween1and6.
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 07H 0003H xxH sum
Note1:Confirmationcode=00H:readytotransferthefollowingdatapacket;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0eH:cannotreceivethefollowingdatapacket
Datapackageformat:
2bytes 4bytes 1byte 2bytes Nbytes 2bytes
Header Module Packageidentifier Package Package Checksum
address length content
0xEF01 xxxx 0x02-have N+2 Template sum
followingpacket data
0x08-endpacket
Note2.Theuppercomputersendsthecommandpacket,themodulesendstheacknowledge
packetfirst,andthensendsseveraldatapacket.
3.Packet Bytes N is determined by Packet Length. The value is 128 Bytes before
delivery.
4.Theinstructiondoesn’taffectbuffercontents.
To store template Store
Description:tostorethetemplateofspecifiedbuffer(Buffer1/Buffer2)atthedesignatedlocationof
Flashlibrary.
InputParameter:CharBufferID,ModelID
ReturnParameter:Confirmationcode(1byte)
Instructioncode:06H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes 2bytes
Header Module Package Package Instruction buffer Location Checksum
address identifier length code number number
0xEF01 xxxx 01H 0006H 06H CharBuffer ModelID sum
ID
Note: CharBufferIDisfilledwith0x01
-21- www.hzgrow.com

## Page 27

Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:storagesuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0bH:addressingModelIDisbeyondthefingerlibrary;
Confirmationcode=18H:errorwhenwritingFlash.
To read template from Flash library LoadChar
Description: to load template at the specified location (PageID) of Flash library to template buffer
CharBuffer1/CharBuffer2
InputParameter:CharBufferID,ModelID
ReturnParameter:Confirmationcode(1byte)
Instructioncode:07H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes 2bytes
Header Module Package Package Instruction buffer Page Checksum
address identifier length code number number
0xEF01 xxxx 01H 0006H 07H CharBuffer ModelID sum
ID
Note: CharBufferIDisfilledwith0x01
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Confirmation Checksum
identifier length code
0xEF01 xxxx 07H 0003H XxH sum
Note:Confirmationcode=00H:loadsuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmation code=0cH:error whenreading template from library or the readouttemplate is
invalid;
Confirmationcode=0BH:addressingModelIDisbeyondthefingerlibrary;
To delete template DeletChar
Description: to delete a segment (N) of templates of Flash library started from the specified
location(orPageID);
InputParameter:StartID+Num
ReturnParameter:Confirmationcode(1byte)
Instructioncode:0cH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes 2bytes 2bytes
Header Module Package Package Instruction Page numberof Checksum
-22- www.hzgrow.com

## Page 28

address identifier length code number templatesto
bedeleted
0xEF01 Xxxx 01H 0007H 0cH StartID Num sum
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Packageidentifier Package Confirmation Checksum
address length code
0xEF01 Xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:deletesuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=10H:failetodeletetemplates;
Confirmationcode=18H:errorwhenwriteFLASH
To empty finger library Empty
Description:todeleteallthetemplatesintheFlashlibrary
InputParameter:none
ReturnParameter:Confirmationcode(1byte)
Instructioncode:0dH
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Instruction Checksum
address identifier length code
0xEF01 Xxxx 01H 0003H 0dH 0011H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 Xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:emptysuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=11H:failtoclearfingerlibrary;
Confirmationcode=18H:errorwhenwriteFLASH
To carry out precise matching of two finger templates Match
Description: Compare the recently extracted character with the templates in the ModelBuffer,
providingmatchingresults.
InputParameter:none
ReturnParameter:Confirmationcode(1byte)，matchingscore.
Instructioncode:03H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
-23- www.hzgrow.com

## Page 29

0xEF01 Xxxx 01H 0003H 03H 0007H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes 2bytes
Header Module Package Package Confirmation Matching Checksum
address identifier length code score
0xEF01 Xxxx 07H 0005H XxH MatchScore sum
Note1:Confirmationcode=00H:templatesofthetwobuffersarematching!
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=08H:templatesofthetwobuffersaren’tmatching;
2:Theinstructiondoesn’taffectthecontentsofthebuffers.
To search finger library Search
Description:tosearchthewholefingerlibraryforthetemplatethatmatchestheoneinCharBuffer1
orCharBuffer2.Whenfound,PageIDwillbereturned.
InputParameter:CharBufferID+StartID+Num
ReturnParameter: Confirmationcode+ModelID(templatenumber)+MatchScore
Instructioncode:04H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes 2bytes 2bytes
Header Module Package Package Instructio buffer Parameter Parameter Checks
address identifie length ncode number um
r
0xEF01 xxxx 01H 0008H 04H CharBuff StartID Num sum
erID
Note:CharBufferIDisfilledwith0x01
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes 2bytes 2bytes
Header Module Package Package Confirmation Page Score Checksum
address identifier length code
0xEF01 xxxx 07H 0007H xxH Model MatchScore sum
ID
Note1:Confirmationcode=00H:foundthematchingfiner;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmation code=09H: No matching in the library (both the PageID and
matchingscoreare0);
2:Theinstructiondoesn’taffectthecontentsofthebuffers.
Fingerprint image collection extension command GetImageEx(0x28)
Description: Detect the finger, record the fingerprint image and store it in ImageBuffer, return it
and record the successful confirmation code;If no finger is detected, return no finger confirmation
code(the module responds quickly to each instruction,therefore, for continuous detection, cycle
processingisrequired,whichcanbelimitedtothenumberofcyclesorthetotaltime).
DifferencesbetweenGetImageExandtheGetImage:
-24- www.hzgrow.com

## Page 30

GetImage: return the confirmation code 0x00 when the image quality is too bad (image
collectionsucceeded)
GetImageEx: return the confirmation code 0x07 when the image quality is too bad (poor
collectionquality)
InputParameter:none
ReturnParameter:Confirmationcode
Instructioncode:0x28
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x28 002CH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:readsuccess
Confirmationcode=0x01:errorwhenreceivingpackage;
Confirmationcode=0x02:nofingersonthesensor;
Confirmationcode=0x03:unsuccessfulentry
Confirmationcode=0x07:poorimagequality;
Cancel instruction Cancel(0x30)
Description:Cancelinstruction
InputParameter:none
ReturnParameter:Confirmationcode
Instructioncode:0x30
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x30 0034H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:cancelsettingsuccessful
Confirmationcode=other:cancelsettingfailed
-25- www.hzgrow.com

## Page 31

HandShake HandShake (0x40)
Description: Send handshake instructions to the module. If the module works normally, the
confirmation code 0x00 will be returned. The upper computer can continue to send instructions to
themodule.Iftheconfirmationcodeisotherornoreply,itmeansthatthedeviceisabnormal.
InputParameter:none
ReturnParameter:Confirmationcode
Instructioncode:0x40
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x40 0044H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:thedeviceisnormalandcanreceiveinstructions;
Confirmationcode=other:thedeviceisabnormal.
In addition, after the module is powered on, 0x55 will be automatically sent as a handshake sign.After
the single-chip microcomputer detects 0x55, it can immediately send commands to enter the working
state.
CheckSensor CheckSensor (0x36)
Description:Checkwhetherthesensorisnormal
InputParameter:none
ReturnParameter:Confirmationcode
Instructioncode:0x36
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x36 003AH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:thesensorisnormal;
Confirmationcode=0x29:thesensorisabnormal.
-26- www.hzgrow.com

## Page 32

Soft reset SoftRst (0x3D)
Description: Send soft reset instruction to the module. If the module works normally, return
confirmationcode0x00,andthenperformresetoperation.
InputParameter:none
ReturnParameter:Confirmationcode
Instructioncode:0x3D
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instructioncode Checksum
address identifier
0xEF01 xxxx 0x01 0x0003 0x3D 0041H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:success;
Confirmationcode=other:deviceisabnormal
After module reset, 0x55 will be automatically sent as a handshake sign. After the single-chip
microcomputerdetects0x55,itcanimmediatelysendcommandstoentertheworkingstate.
Aura control AuraLedConfig (0 x35)
Description:AuraLEDcontrol
InputParameter:Controlcode:Ctrl;Speed;ColorIndex;Times
ReturnParameter:Confirmationcode
Instructioncode:0x35
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 1byte 1byte 1byte 2bytes
Header Module Package Package Instruction Control Speed Color Times Checks
address identifier length code code Index um
0xEF0 0x01 0x0007 0x35 Ctrl Speed Color Count sum
xxxx
1 Index
ControlCode:
Control 0x01 0x02 0x03 0x04 0x05 0x06
code
Function breathing Flashing Light Light Light Light
light light Always Always gradually gradually
on off on off
Speed:0x00-0xff,256gears,Minimum5scycle.
Itiseffectiveforbreathinglampandflashinglamp,Lightgraduallyon,Lightgraduallyoff
-27- www.hzgrow.com

## Page 33

ColorIndex:
Code 0x01 0x02 0x03 0x04 0x05 0x06 0x07
Color Red Blue Purple Green Yellow Cyan White
Numberofcycles:0-infinite,1-255.
Itiseffectiveforwithbreathinglightandflashinglight.
Exampleofsettinglight:Cyanbreathinglight:
Send:EF01FFFFFFFF01000735015006000094
Return:EF01FFFFFFFF07000300000A
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 0x07 0x0003 X sum
Note1:Confirmationcode=0x00:success;
Confirmationcode=0x01:errorwhenreceivingpackage;
Automatic registration template AutoEnroll (0 x31)
When a fingerprint is recorded using an automatic registration template, the fingerprint image needs to
be recorded six times for each fingerprint template. The blue light blinks when the fingerprint image is
collected. The yellow light is on means the fingerprint image is collected successfully,the green light
blinks means the fingerprint characteristic is generated successfully. If the finger is required to leave
duringimagecollection,theimagewillbecollectedagainafterthefinger islifted.Duringtheprocessof
waiting for the finger to leave, the white light flashes.After fingerprint images are collected for 6 times
and features are generated successfully, features are synthesized and store fingerprint template. If the
operation succeeds, the green light is on; if the operation fails, the red light is on. If the finger is away
from the sensor for more than 10 seconds when in collecting the fingerprint image each time, it will
automaticallyexitstheautomatictemplateregistrationprocess.
InputParameter:ModelID-Fingerprintlibrarylocationnumber
Config1:WhethertoallowcoverIDnumber
Config2:Whethertoallowduplicatefingerprints
Config3:Whetherthemodulereturnthestatusinthecriticalstep
Config4:Whethertoallowaskthefingertoleave
ReturnParameter:Confirmationcode ModelID(Fingerprintlibrarylocationnumber)
Instructioncode:0x31
Command(orinstruction)packageformat:
2 4 1byte 2bytes 1byte 1byte 1byte 1byte 1byte 1byte 2
bytes bytes bytes
Heade Module Package Package Instruction Locatio Whether Whether Whether Whether Check
r address identifier length code n Duplicat Duplicate return askfinger sum
-28- www.hzgrow.com

## Page 34

ID eID Fingerprint status toleave
0xEF 0x01 0x0008 0x31 ID Config1 Config2 Config Config4 sum
xxxx
01 3
ModelID:LocationID:0-0xC7
0xC8-0xFFisautomaticfilling(TheIDnumberisassignedbythesystem.
Thesystemwillbestartingfromtemplate0tosearchestheemptytemplates.)
WhethertoallowcoverIDnumber:0:Notallowed1:Allow
Whethertoallowregisterduplicatefingerprints:0:Notallowed1:Allow
Whethertoreturntothecriticalstepstatusduringregistration:0:Notallowed1:Allow
Whether the finger is required to leave during the registration process in order to enter the next
fingerprintimagecollection:0:don’tneedtoleave 1:havetoleave
ExampleofAutoEnroll:
Send:EF01FFFFFFFF01000831C8000101010105
Return:EF 01FF FF FF FF 0700 0500 0100 000D EF 01 FF FF FF FF 0700 0500 020000 0EEF
01FFFFFFFF 070005000300000FEF01FFFFFFFF0700050004000010EF01FFFFFF
FF0700 050005000011EF01FF FF FF FF 07000500060000 12EF01FF FF FF FF 070005
0007000013 EF 01FF FF FF FF 07 000500 08000014 EF01 FF FF FF FF 0700 050009 0000
15EF01FFFFFFFF070005000A000016EF01FFFFFFFF070005000B000017EF01FF
FFFFFF070005000C000018EF01FFFFFFFF070005000D000019EF01FFFFFFFF07
0005000E00001AEF01FFFFFFFF070005000F00001B (FingerprintID)
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 1bytes 1bytes 2bytes
Header Module Package Package Confirmation Parameter Parameter Checksum
address identifier length code 1 2
0xEF01 xxxx 0x07 0x0005 X X X sum
Parameter1:StepProcess:
0x01:Collectimageforthefirsttime
0x02:GenerateFeatureforthefirsttime
0x03:Collectimageforthesecondtime
0x04:GenerateFeatureforthesecondtime
0x05:Collectimageforthethirdtime
0x06:GenerateFeatureforthethirdtime
0x07:Collectimageforthefourthtime
0x08:GenerateFeatureforthefourthtime
0x09:Collectimageforthefifthtime
0x0A:GenerateFeatureforthefifthtime
0x0B:Collectimageforthesixthtime
0x0C:GenerateFeatureforthesixthtime
0x0D:Repeatfingerprintcheck
0x0E:Mergefeature
0x0F:Storagetemplate
-29- www.hzgrow.com

## Page 35

Parameter2:fingerprintID
SpecificAcknowledgepackageformat:
Header Module Package Package Confirm Step Fingerprint CheckSum Note
address identifier length ation ID
code
2 Bytes 4 Bytes 1 Byte 2 Bytes 1 Byte 1 Byte 1 Byte 2 Bytes
0xEF01 XXXX 0x07 0x0005 X 0x01 0x00 Sum Collectimagefor
thefirsttime
0xEF01 XXXX 0x0005 X 0x02 0x00 Sum GenerateFeaturefor
0x07
thefirsttime
0xEF01 XXXX 0x0005 X 0x03 0x00 Sum Collectimagefor
0x07
thesecondtime
0xEF01 XXXX 0x0005 X 0x04 0x00 Sum GenerateFeaturefor
0x07
thesecondtime
0xEF01 XXXX 0x0005 X 0x05 0x00 Sum Collectimagefor
0x07
thethirdtime
0xEF01 XXXX 0x0005 X 0x06 0x00 Sum GenerateFeaturefor
0x07
thethirdtime
0xEF01 XXXX 0x0005 X 0x07 0x00 Sum Collectimagefor
0x07
thefourthtime
0xEF01 XXXX 0x0005 X 0x08 0x00 Sum GenerateFeaturefor
0x07
thefourthtime
0xEF01 XXXX 0x0005 X 0x09 0x00 Sum Collectimagefor
0x07
thefifthtime
0xEF01 XXXX 0x0005 X 0x0A 0x00 Sum GenerateFeature
0x07
forthefifthtime
0xEF01 XXXX 0x0005 X 0x0B 0x00 Sum Collectimagefor
0x07
thesixthtime
0xEF01 XXXX 0x0005 X 0x0C 0x00 Sum GenerateFeature
0x07
forthesixthtime
0xEF01 XXXX 0x0005 X 0x0D 0x00 Sum Repeatfingerprint
0x07
check
0xEF01 XXXX 0x07 0x0005 X 0x0E 0x00 Sum Mergefeature
0xEF01 XXXX 0x07 0x0005 X 0x0F modelID Sum Storagetemplate
If the status of return key step is set to 0 during registration, only returned the the last
acknowledgepacket.
Confirmationcode=0x00setsuccessfully
Confirmationcode=0x01setfails
Confirmationcode=0x07failedtogenerateafeature
Confirmationcode=0x0afailedtomergetemplates
Confirmationcode=0x0btheIDisoutofrange
Confirmationcode=0x1ffingerprintlibraryisfull
Confirmationcode=0x22fingerprinttemplateisempty
Confirmationcode=0x26timesout
Confirmationcode=0x27fingerprintalreadyexists
-30- www.hzgrow.com

## Page 36

Automatic fingerprint verification AutoIdentify (0 x32)
When the automatic fingerprint verification command is used to search and verify a fingerprint, the
system automatically collects a fingerprint image and generates features, and compares the image with
the fingerprint template in the fingerprint database. If the comparison is successful, the system returns
the template ID number and the comparison score. If the comparison fails, the system returns the
correspondingerrorcode.
When obtaining the fingerprint image, the fingerprint head will light up with a white breathing light.
After the image collection is successful, the yellow light will light up, and the green light will light up
afterthecomparisonissuccessful.Ifthereisafingerprintimagecollectionerrorornofingerprintsearch,
theredlightwillbeontoprompt.
If the system does not detect the finger for more than 10 seconds after sending the command or
collectingthefingerprintimageagainafterreportinganerror,itwillautomaticallyexitthecommand.
InputParameter:
SafeGrade(1-5level)
StartID
Num-Numberofsearches
Config1Whetherthemodulereturnstothestatusinkeysteps
Config2Numberoffingerprintsearcherror
ReturnParameter:Confirmationcode ModelID MarchScore
Instructioncode:0x32
Command(orinstruction)packageformat:
2 4 1byte 2bytes 1byte 1byte 1byte 1byte 1byte 1byte 2
bytes bytes bytes
Heade Package Package Instruction Security Start Numberof Whether Number Check
r identifier length code Level Position searches return of sum
Module keystep fingerpri
address nt
search
error
0xEF 0x01 0x0008 0x32 Safe StartID num Config Config2 sum
xxxx
01 Grade 1
Securitylevel:1-5
Startingposition:0-199
Endposition:1-200
Returnsearchsteps:0:notallowed1:Allowed
Fingerprintsearcherrortimes:
0-0xFF: 0: the operation of image collection and feature search is carried out all the time. If the same
feature ID number is found in the fingerprint database, the operation will exit (0 means the cycle
continuesuntilthematchingIDisfoundorthepoweriscutoff).
1-0xFF: The generated features and fingerprint search were performed on the collected images. If the
match is successful, the ID number and score of the match will be returned, and exit this instruction at
the same time. If the match fails, repeat the previous operations for 1-0xff times. Exit after
-31- www.hzgrow.com

## Page 37

correspondingerrortimes.Nomatterwhatway,ifthesystemdoesnotdetectthefingeraftersendingthe
commandorcollectingtheimageagainformorethan10seconds,itwillexitthecommand.
ExampleofAutoIdentify:
Send:EF01FFFFFFFF010008320300C801010108
Return:EF01FFFFFFFF0700080001000000000010EF01FFFFFFFF07000800020000
00000011EF01FFFFFFFF07000800030000003D004F(FingerprintIDandscore)
Acknowledgepackageformat:
2bytes 4 1byte 2bytes 1byte 1byte 2bytes 2bytes 2bytes Note
bytes
Header Module Package Package Confirmat Step Position Score Check
address identifier length ioncode Number Sum
0xEF01 0x07 0x0008 X 1 00 00 Sum Collect
xxxx
Image
0xEF01 0x07 0x0008 X 2 00 00 Sum Generate
xxxx
Feature
0xEF01 0x07 0x0008 X 3 Model Match Sum Search
xxxx
ID score
Confirmationcode=0x00setsuccessfully
Confirmationcode=0x01setfails
Confirmationcode=0x09failedtosearchfingerprint
Confirmationcode=0x0btheIDisoutofrange
Confirmationcode=0x22fingerprinttemplateisempty
Confirmationcode=0x24fingerprintlibraryisempty
Confirmationcode=0x26timesout
Other instructions
To generate a random code GetRandomCode
Description:tocommandtheModuletogeneratearandomnumberandreturnittouppercomputer;
InputParameter:none
ReturnParameter:Confirmationcode(1byte)+RandomCode
Instructioncode:14H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instruction Checksum
address identifier code
0xEF01 xxxx 01H 0003H 14H 0018H
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 4bytes 2bytes
Header Module Package Package Confirmation Random Checksum
address identifier length code number
-32- www.hzgrow.com

## Page 38

0xEF01 xxxx 07H 0007H xxH RandomC sum
ode
Note:Confirmationcode=00H:generationsuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
To read information page ReadInfPage
Description:readinformationpage(512bytes)
InputParameter:none
ReturnParameter:Confirmationcode(1byte)
Instructioncode:16H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Packagelength Instruction Checksum
address identifier code
0xEF01 xxxx 01H 0003H 16H 001AH
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Module Package Package Confirmation Checksum
address identifier length code
0xEF01 xxxx 07H 0003H xxH sum
Note1:Confirmationcode=00H:readytotransferthefollowingdatapacket;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=0fH:cannottransferthefollowingdatapacket;
2.The upper computer sends the command packet, the module sends the acknowledge
packetfirst,andthensendsseveraldatapacket.
3.Packet Bytes N is determined by Packet Length. The value is 128 Bytes before
delivery.
4:Theinstructiondoesn’taffectbuffercontents.
Datapackageformat:
2bytes 4bytes 1byte 2bytes Nbytes 2bytes
Header Module Packageidentifier Package Package Checksum
address length content
0xEF01 xxxx 0x02-have N+2 Information sum
followingpacket page
0x08-endpacket
To write note pad WriteNotepad
Description:foruppercomputertowritedatatothespecifiedFlashpage.AlsoseeReadNotepad;
InputParameter:NotePageNum,usercontent(ordatacontent)
ReturnParameter:Confirmationcode(1byte)
Instructioncode:18H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 32bytes 2bytes
-33- www.hzgrow.com

## Page 39

Header Module Package Package Instruction Page Data Checksum
address identifier length code number content
0xEF01 xxxx 01H 0x0024 18H 0x00-0 content sum
x0F
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 2bytes
Header Moduleaddress Package Package Confirmationcode Checksum
identifier length
0xEF01 xxxx 07H 0003H xxH sum
Note:Confirmationcode=00H:writesuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
Confirmationcode=18H:errorwhenwriteFLASH
To read note pad ReadNotepad
Description:toreadthespecifiedpage’sdatacontent;AlsoseeWriteNotepad.
InputParameter:NotePageNum
ReturnParameter:Confirmationcode(1byte)+Usercontent
Instructioncode:19H
Command(orinstruction)packageformat:
2bytes 4bytes 1byte 2bytes 1byte 1byte 2bytes
Header Module Package Package Instruction Page Checksum
address identifier length code number
0xEF01 xxxx 01H 0004H 19H 0x00-0x Sum
0F
Acknowledgepackageformat:
2bytes 4bytes 1byte 2bytes 1byte 32bytes 2bytes
Header Module Package Package Confirmation Usercontent Checksum
address identifier length code
0xEF01 xxxx 07H 0x0023 xxH Usercontent sum
Note:Confirmationcode=00H:readsuccess;
Confirmationcode=01H:errorwhenreceivingpackage;
-34- www.hzgrow.com

## Page 40

Ⅵ Operation Process
6.1 Basic communication flow
6.1.1ProcessoftheUARTcommandpackage
Receivinginstructionpacket
Executeinstruction
Handlesuccessful
Acknowledgepackagesendfailed
Acknowledgepackagesendsucceed
Finish
-35- www.hzgrow.com

## Page 41

6.1.2UARTPacketSendingProcess
Before transmitting data packets, the UART should be received the instruction packet for
transmitting data packets first, makes preparations for transmission, then sends a successful response
packet, and finally starts transmitting the data packets. Packet mainly includes: packet header, chip
address,packetidentity,packetlength,dataandchecksum.
There are two types of packet identifiers: 02H and 08H. 02H: indicates the data packet and
subsequentpackets. 08H:indicates the lastpacket, thatis, theend packet. Data length is pre-set, mainly
dividedinto:32,64,128,and256fourtypes.
Forexample,ifthelengthofthedatatobetransmittedis1Kbytesandthepresetlengthofthedata
packetis 128 bytes, the 1K bytes ofdata must be divided into eight data packets. Each packetincludes:
2bytes header,4bytes chip address,1 bytes packetidentifier,2 bytes packetlength, 128bytes data and
2byteschecksum,eachpacketlengthis139bytes.
In addition, of the eight packets, the packet ID of the first seven packets is 02H and the packet ID
ofthelastenddatapacketis08H.Finally,notethatiftheendpacketdoesnotreach139bytesinlength,
itistransmittedattheactuallengthandisnototherwiseexpandedto139bytes.
Receivinginstructionpacket
Executeinstruction
Handlesuccessful
Acknowledgepackagesendsucceed
Assembleasetofpackets,sending.
Acknowledgepackagesendfailed
Endpacket
Finish
-36- www.hzgrow.com

## Page 42

6.1.3UARTpacketreceivingprocess
Before transmitting data packets, the UART should be received the instruction packet for
transmitting data packets first, makes preparations for transmission, then sends a successful response
packet, and finally starts transmitting the data packets. Packet mainly includes: packet header, chip
address,packetidentity,packetlength,dataandchecksum.
There are two types of packet identifiers: 02H and 08H. 02H: indicates the data packet and
subsequentpackets. 08H:indicates the lastpacket, thatis, theend packet. Data length is pre-set, mainly
dividedinto:32,64,128,and256fourtypes.
Forexample,ifthelengthofthedatatobetransmittedis1Kbytesandthepresetlengthofthedata
packetis 128 bytes, the 1K bytes ofdata must be divided into eight data packets. Each packetincludes:
2bytes header,4bytes chip address,1 bytes packetidentifier,2 bytes packetlength, 128bytes data and
2byteschecksum,eachpacketlengthis139bytes.
In addition, of the eight packets, the packet ID of the first seven packets is 02H and the packet ID
ofthelastenddatapacketis08H.Finally,notethatiftheendpacketdoesnotreach139bytesinlength,
itistransmittedattheactuallengthandisnototherwiseexpandedto139bytes.
Receivinginstructionpacket
Executeinstruction
Handlesuccessful
Acknowledgepackagesendsucceed
Receive a set of packets and parse
them
Acknowledgepackagesendfailed
Endpacket
Finish
-37- www.hzgrow.com

## Page 43

6.2 General instruction communication flow
6.2.1Generalinstructionregisterfingerprintprocess
The fingerprint registration process mainly includes: obtaining images for registration, generating
features,mergingfeaturesandstoringtemplates.Usually N=2times.
Start
Sendmergingfeaturescommand
Sendacquireimagecommand
Returnsuccessfully Returnsuccessfully
Sendstoretemplatescommand
Sendgeneratefeaturecommand
Returnsuccessfully
Returnsuccessfully
Finish
Whentheregistrationlogicissetto1,register fingerprint.Ifthecurrentfingerprintis similartothe
fingerprintthathasbeenincludedbefore,theconfirmationcodeintheresponsepacketthatgeneratesthe
feature command does not show success, but returns 28H, indicating that there is a correlation between
the current fingerprint feature and the previous feature. It should be noted that the mutual comparison
correlation is limited to the fingerprints included in this registration process, and will not be compared
withthefingerprintsinthefingerprintlibrary.
Whentheregistrationlogicissetto2,registerfingerprint.Ifthecurrentfingerprintisnotsimilarto
thefingerprintthathasbeenincludedbefore,theconfirmationcodeintheresponsepacketthatgenerates
the feature command does not show success, but returns 08H, indicating that there is no correlation
between the current fingerprint feature and the previous feature. It should be noted that the mutual
comparisoncorrelationis limitedtothefingerprints includedin this registrationprocess,andwill notbe
comparedwiththefingerprintsinthefingerprintlibrary.
Whether it returns 28H or 08H, the currentfingerprint feature has been successfully extracted, you
can take a new map and generate features without changing the BufferID, or you can skip the current
BufferIDandincludethenextroundoffingerprints.
-38- www.hzgrow.com

## Page 44

6.2.2Generalinstructionverityfingerprintprocess
The fingerprint verification process of general instructions mainly includes: obtaining images for
verification, generating features and searching fingerprints. When sending generated features and
searchingforfingerprints,BufferIDissetto1bydefault.
Start
Sendacquireimagecommand
Returnsuccessfully
Sendgeneratefeaturecommand
Returnsuccessfully
Sendsearchfingerprintcommand
Finish
-39- www.hzgrow.com

## Page 45

6.2.3ReadaspecifiedtemplateuploadtoFlashFingerprintDatabase
Thewholeprocessmainlyincludes:readtemplateanduploadtemplates.
BufferIDissettothedefaultvalue2whenreadingtemplateanduploadingfeature.
Start
Sendreadtemplatecommand
Returnsuccessfully
Senduploadfeaturecommand
Returnsuccessfully
Receivedatapacket
Finishpacket
Finish
-40- www.hzgrow.com

## Page 46

6.3Automatic Register Fingerprint
Instructionerror,didnotreturnthereplypacket Sendinstruction
Overwriting ID
If the override ID is not
numbers is not
allowed, check whether
allowed, Error
the ID already exists. If
code 22 or Error
the ID does not exist,
code 0B is
skipit
returned when the
IDisfull
Bluelightflashing
If no image is captured for
Startcollectimage
more than 15 seconds, error
code26isreturned
Repeat
Collectedimage Yellowlightturnon500ms
Redlightturnon500ms 6
times
Greenlightflash200ms
successf
Error
Failedtogeneratefeatures Generatefeaturesuccessfully ully
respond
Whitelightturnon
Ifneedwaitthefingerremove
Repeat6timessuccessfully
Duplicatefingerprintsreturnerrorcode27
Redlightturnon500ms
Greenlightflash
Mergerfailreturnerrorcode0A
Checkduplicatefingerprint,ifdon’thave,skipit
Redlightturnon500ms
Storefailreturnerrorcode18 Mergefeature
Redlightturnon500ms
Green light
turn on
Storetemplatesuccessfully
500ms
-41- www.hzgrow.com

## Page 47

6.4Automatic Fingerprint Verification(Search)
Fingerprinterror,norespond Sendinstruction
Levelparametererror,return01
Positionerroe,return0B
Check the security
level and Start end
positionparameters
Whitebreathinglight
If no image is captured for Collectimage
Error
more than 15 seconds, the
respond
system automatically exits,
returnerrorcode26
Collectimagesuccessfully Yellow light
turn on
500ms
Generatefeaturefail Generatefeaturesuccessfully
Fingerprintsearch
Searchfail
Greenlightturnon500ms
Searchsuccessfully
ReturnIDandScore
Fail:redlightturnon
Finish
-42- www.hzgrow.com

## Page 48

6.5 Low power standby
For low-power standby scenarios, the host can cut off the main power supply of the module (can
not cut off the touch-sensitive power supply). Once the module detects a finger, it outputs a signal
intheIRQsignal.Thenthehostcanpoweronthemoduletoperformfingerprintidentification.
Start
Poweronthemainpowersupply(VCC)ofthemodule
Performfingerprintregistrationandidentificationandsoon
Whethertoentertolow-powerstandbymode
Poweroffthemainpowersupply(VCC)ofthemodule
IftheIRQishigh
-43- www.hzgrow.com

## Page 49

Ⅶ Reference Circuit
Inlow-powersupplymode,thewholecircuitisnormallypoweredoff.Usethefingerdetectionfunction
ofthemoduletopoweronthewholemachine.PleaserefertothecircuitformofR307(R307is5V
powersupply).
-44- www.hzgrow.com