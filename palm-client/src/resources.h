#ifndef VM_RESOURCES_H
#define VM_RESOURCES_H

#define MainForm          1000
#define MainNameLabel     1001
#define MainConnectButton 1002
#define MainSerialPush    1004
#define MainUsbPush       1005
#define MainSlotPush      1013
#define MainFreqLabel     1006
#define MainModeLabel     1007
#define MainPowerLabel    1008
#define MainFirmwareLabel 1009
#define MainSettingsButton 1010
#define MainAdvertButton   1011
#define MainDoneButton     1012
#define MainMenuBar        1000
#define MainMenuPorts      1001
#define MainMenuFontPreview 1002
#define MainMenuHome       1003
#define MainMenuContacts   1004
#define MainMenuNode       1005
#define MainMenuAddChannel 1006
#define MainMenuLeaveChannel 1007
#define MainMenuSounds     1008
#define MainMenuMuteChannel 1009
#define MainMenuChannelScope 1010
#define MainMenuScopes     1011
#define MainMenuMap        1012
#define MainLinkGroup     1

#define SettingsForm          1200
#define SettingsNameField     1201
#define SettingsPresetTrigger 1202
#define SettingsPresetList    1203
#define SettingsFreqField     1204
#define SettingsBwField       1205
#define SettingsSfField       1206
#define SettingsCrField       1207
#define SettingsPowerField    1208
#define SettingsHash1Push     1209
#define SettingsHash2Push     1210
#define SettingsHash3Push     1211
#define SettingsStatusLabel   1212
#define SettingsSaveButton    1213
#define SettingsCancelButton  1214
#define SettingsHashGroup     2
#define SettingsUtcField      1215

#define SettingsErrorAlert    2001
#define ContactInfoAlert      2002
#define RebootRadioAlert      2003

#define ContactsForm          1300
#define ContactsGadget        1301
#define ContactsScroll        1302
#define ContactsRefreshButton 1303
#define ContactsDoneButton    1304
#define ContactsAddButton     1305

#define ContactForm           1800
#define ContactGadget         1801
#define ContactScroll         1802
#define ContactDoneButton     1803
#define ContactMessageButton  1804
#define ContactLoginButton    1805
#define ContactMoreButton     1806
#define ContactMoreList       1807

#define ContactAddForm        1900
#define ContactAddNameField   1901
#define ContactAddChatPush    1902
#define ContactAddRoomPush    1903
#define ContactAddRepeaterPush 1904
#define ContactAddKeyField    1905
#define ContactAddAddButton   1906
#define ContactAddCancelButton 1907
#define ContactAddTypeGroup   4

#define RemoteForm            2100
#define RemoteGadget          2101
#define RemoteScroll          2102
#define RemoteField           2103
#define RemoteSendButton      2104
#define RemoteStatusButton    2105
#define RemoteTelemButton     2106
#define RemoteCmdsButton      2107
#define RemoteDoneButton      2108
#define RemoteCmdList         2109

#define PasswordAlert         2007
#define RemoveContactAlert    2008

#define HomeForm              1500
#define HomeGadget            1501
#define HomeScroll            1502
#define HomeContactsButton    1503
#define HomeNodeButton        1504

#define ConvoForm             1600
#define ConvoGadget           1601
#define ConvoScroll           1602
#define ConvoField            1603
#define ConvoSendButton       1604
#define ConvoBackButton       1605

#define AdvertAlert           2004
#define RetryAlert            2005
#define LeaveChannelAlert     2006

#define ChannelForm           1700
#define ChannelNameField      1701
#define ChannelHashtagPush    1702
#define ChannelPrivatePush    1703
#define ChannelKeyLabel       1704
#define ChannelKeyField       1705
#define ChannelAddButton      1706
#define ChannelCancelButton   1707
#define ChannelTypeGroup      3

#define PortsForm         1100
#define PortsDoneButton   1101

#define RomIncompatibleAlert 2000

#define SoundsForm            2200
#define SoundsFirstCheck      2201   /* + VM_SND_* (sounds.h), in that order */
#define SoundsDoneButton      2210
#define MuteAlert             2211
#define SoundsSmallPush       2212
#define SoundsLargePush       2213
#define SoundsTextGroup       5

#define ScopesForm            2300
#define ScopesDefaultLabel    2301
#define ScopesList            2302
#define ScopesField           2303
#define ScopesAddButton       2304
#define ScopesDeleteButton    2305
#define ScopesSetDefaultButton 2306
#define ScopesClearDefaultButton 2307
#define ScopesDoneButton      2308
#define ScopeAlert            2309
#define ScopesDiscoverButton  2310

#define ChanScopeForm         2400
#define ChanScopeList         2401
#define ChanScopeOKButton     2402
#define ChanScopeCancelButton 2403

#define MapForm               2500
#define MapGadget             2501
#define MapDoneButton         2502
#define MapOutButton          2503
#define MapInButton           2504
#define MapAlert              2505

#define SmallFontRes          1000
#define LargeFontRes          1001
#define EmojiArtRes           1002
#define FontPreviewForm       1400
#define FontPreviewDoneButton 1401

#endif
