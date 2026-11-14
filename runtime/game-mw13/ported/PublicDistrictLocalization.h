#pragma once

static constexpr const char* publicDistrictNames[]={
 "Palmont City", "Morgan Beach", "The Projects", "Kempton Holdings", "Eskuri Plaza",
 "Newport Industrial Park", "Old Quarter", "Historic Chinatown", "Mason District", "Billings District",
 "Kings Park", "Palmont University", "Ocean View", "Hills Borough", "South Fortuna", "Fortuna Heights",
 "Diamond Hills", "Starlight Strip", "Neon Mile", "Canmor Downs", "Shady Pine", "Infinity Park",
 "Silverton Refinery"
};
static constexpr const wchar_t* publicJapaneseDistricts[]={
 L"パルモントシティ", L"モーガンビーチ", L"プロジェクト", L"ケンプトンホールディングス", L"エスクリプラザ",
 L"ニューポート工業地区", L"オールドクォーター", L"チャイナタウン", L"メイソン地区", L"ビリングス地区",
 L"キングスパーク", L"パルモントユニバーシティ", L"オーシャンビュー", L"ヒルズボロー", L"南フォーチュナ", L"フォーチュナハイツ",
 L"ダイヤモンドヒルズ", L"スターライトストリップ", L"ネオンマイル", L"カンモアダウンズ", L"シェイディパイン", L"インフィニティパーク",
 L"シルバートン精製所"
};
static_assert(std::size(publicJapaneseDistricts)==std::size(publicDistrictNames));

// Localize geographic descriptors. Proper names without a localized form
// retain their English spelling, as in Carbon's locale tables.
struct PublicDistrictLocale {
 int gameLanguage;
 const char* locale;
 const wchar_t* city,*industrial,*oldQuarter,*mason,*billings,*university,*south,*refinery;
};
static constexpr PublicDistrictLocale publicDistrictLocales[]={
 {1,"fr",L"Ville de Palmont",L"Zone industrielle de Newport",L"Vieux Quartier",L"Quartier Mason",L"Quartier Billings",L"Université de Palmont",L"Fortuna Sud",L"Raffinerie de Silverton"},
 {2,"de",L"Stadt Palmont",L"Industriepark Newport",L"Altstadt",L"Mason-Viertel",L"Billings-Viertel",L"Universität Palmont",L"Süd-Fortuna",L"Raffinerie Silverton"},
 {3,"it",L"Città di Palmont",L"Zona industriale di Newport",L"Quartiere Vecchio",L"Quartiere Mason",L"Quartiere Billings",L"Università di Palmont",L"Fortuna Sud",L"Raffineria di Silverton"},
 {4,"es",L"Ciudad de Palmont",L"Parque industrial de Newport",L"Barrio Antiguo",L"Distrito Mason",L"Distrito Billings",L"Universidad de Palmont",L"Fortuna Sur",L"Refinería de Silverton"},
 {5,"nl",L"Palmont-stad",L"Industriepark Newport",L"Oude Wijk",L"Masonwijk",L"Billingswijk",L"Universiteit van Palmont",L"Fortuna-Zuid",L"Raffinaderij Silverton"},
 {6,"sv",L"Palmonts stad",L"Newports industriområde",L"Gamla kvarteren",L"Mason-distriktet",L"Billings-distriktet",L"Palmonts universitet",L"Södra Fortuna",L"Silvertons raffinaderi"},
 {7,"da",L"Palmont by",L"Newport Industripark",L"Det Gamle Kvarter",L"Mason-kvarteret",L"Billings-kvarteret",L"Palmont Universitet",L"Sydfortuna",L"Silverton Raffinaderi"},
 {12,"pl",L"Miasto Palmont",L"Park przemysłowy Newport",L"Stara Dzielnica",L"Dzielnica Mason",L"Dzielnica Billings",L"Uniwersytet Palmont",L"Południowa Fortuna",L"Rafineria Silverton"},
 {13,"fi",L"Palmontin kaupunki",L"Newportin teollisuusalue",L"Vanhakaupunki",L"Masonin kaupunginosa",L"Billingsin kaupunginosa",L"Palmontin yliopisto",L"Etelä-Fortuna",L"Silvertonin jalostamo"},
};

static wchar_t PublicDistrictAscii(wchar_t c){
 switch(c){
  case L'à':case L'ä':case L'å':return L'a';
  case L'é':return L'e';
  case L'í':return L'i';
  case L'ł':return L'l';
  case L'ö':return L'o';
  case L'ü':return L'u';
  default:return c;
 }
}

// The game owns the active codebook, including when an ASI redirects its
// English slot to Japanese. The HUD expects its byte encoding, not UTF-8.
static const unsigned* PublicDistrictCodec(){return *reinterpret_cast<const unsigned* const*>(0x91CF84);}
static bool PublicDistrictJapanese(const unsigned* codec){__try{
 if(!codec||*codec<=256||*codec>16384)return false;
 const auto table=reinterpret_cast<const wchar_t*>(codec+1);
 unsigned hiragana=0,katakana=0;
 for(unsigned i=128;i<*codec;++i){
  hiragana+=table[i]>=0x3041&&table[i]<=0x3096;
  katakana+=table[i]>=0x30A1&&table[i]<=0x30FA;
 }
 return hiragana>=55&&katakana>=55;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}}

static unsigned PublicDistrictCode(wchar_t c,const wchar_t* table,unsigned count){
 if(c<128)return c;
 for(unsigned lead=128;lead<256;++lead){
  const unsigned value=table[lead];
  if(value==c)return lead;
  if(value&&value<128){
   const unsigned start=value*128;
   for(unsigned trail=0;trail<128&&start+trail<count;++trail){
    if(table[start+trail]==c)return (lead<<8)|(trail+128);
   }
  }
 }
 return 0;
}

static bool EncodePublicDistrict(const wchar_t* name,const unsigned* codec,char (&out)[96]){__try{
 const unsigned count=codec?*codec:0;
 const auto table=count>=256&&count<=16384?reinterpret_cast<const wchar_t*>(codec+1):nullptr;
 unsigned offset=0;
 for(;*name&&offset<sizeof(out)-2;++name){
  const auto c=*name;
  unsigned code=c<128?c:table?PublicDistrictCode(c,table,count):0;
  if(!code){auto ascii=PublicDistrictAscii(c);if(ascii>=128)return false;code=ascii;}
  if(code>255)out[offset++]=char(code>>8);
  out[offset++]=char(code&255);
 }
 out[offset]=0;
 return !*name;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}}

static const char* PublicLocalizedDistrict(unsigned index,const char*& locale){
 const auto codec=PublicDistrictCodec();
 const wchar_t* translated=nullptr;
 locale="en";
 static const unsigned* lastCodec=nullptr;
 static bool japanese=false;
 if(lastCodec!=codec){lastCodec=codec;japanese=PublicDistrictJapanese(codec);}
 if(japanese){locale="ja";translated=publicJapaneseDistricts[index];}
 else for(const auto& row:publicDistrictLocales){
  if(row.gameLanguage!=*reinterpret_cast<const int*>(0x8F41C0))continue;
  locale=row.locale;
  switch(index){
   case 0:translated=row.city;break;
   case 5:translated=row.industrial;break;
   case 6:translated=row.oldQuarter;break;
   case 8:translated=row.mason;break;
   case 9:translated=row.billings;break;
   case 11:translated=row.university;break;
   case 14:translated=row.south;break;
   case 22:translated=row.refinery;break;
  }
  break;
 }
 static char encoded[std::size(publicDistrictNames)][96]{};
 if(translated&&EncodePublicDistrict(translated,codec,encoded[index]))return encoded[index];
 return publicDistrictNames[index];
}
