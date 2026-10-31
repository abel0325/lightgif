#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "studio_model.h"
#include "renderer.h"
#include "gif_encoder.h"

using namespace Gdiplus;
using namespace gifstudio;

namespace {
constexpr int kMaxTexts=12;
constexpr UINT WM_EXPORT_DONE=WM_APP+11, WM_EXPORT_PROGRESS=WM_APP+12;
enum {ID_ADD=101,ID_FONT,ID_SIZE,ID_SIZE_SLIDER,ID_FIT,ID_BOLD,ID_HOLD,ID_HOLD_SLIDER,
 ID_TRANS,ID_TRANS_SLIDER,ID_PAUSE,ID_RESTART,ID_WIDTH,ID_HEIGHT,ID_TRANSPARENT,ID_LIMIT,
 ID_START_COLOR,ID_END_COLOR,ID_TEXT_COLOR,ID_BORDER_COLOR,ID_BORDER_SIZE,ID_EXPORT,
 ID_OPEN,ID_SAVE,ID_RESET};
HINSTANCE g_instance;
HWND g_window=nullptr,g_rows=nullptr;
HWND g_edit[kMaxTexts]{},g_up[kMaxTexts]{},g_remove[kMaxTexts]{};
std::vector<HWND> g_controls;
HFONT g_font=nullptr;
HBRUSH g_white=nullptr;
Settings g_settings;
LayoutInfo g_layout{};
int g_textCount=3,g_scroll=0;
float g_scale=1.0f;
bool g_loading=false,g_paused=false,g_exporting=false,g_previewDirty=true;
double g_pausedTime=0;
std::chrono::steady_clock::time_point g_start;
std::thread g_worker;
std::atomic<bool> g_cancel{false};
std::wstring g_status=L"准备就绪，调整后即可导出。",g_validation;
std::unique_ptr<Bitmap> g_preview;
std::vector<unsigned char> g_previewPixels;
int g_previewWidth=0,g_previewHeight=0;
COLORREF g_customColors[16]{};

HWND C(int id){return GetDlgItem(g_window,id);}
int P(float v){return static_cast<int>(std::lround(v*g_scale));}
int P(int v){return P(static_cast<float>(v));}
std::wstring Text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::wstring Trim(std::wstring s){auto a=s.find_first_not_of(L" \t\r\n");if(a==std::wstring::npos)return L"";return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);}
std::wstring F(double d,int digits=1){std::wostringstream o;o<<std::fixed<<std::setprecision(digits)<<d;return o.str();}
bool Checked(int id){return SendMessageW(C(id),BM_GETCHECK,0,0)==BST_CHECKED;}
void Check(int id,bool checked){SendMessageW(C(id),BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}
void SetText(int id,const std::wstring& s){SetWindowTextW(C(id),s.c_str());}
double Now(){return g_paused?g_pausedTime:std::chrono::duration<double>(std::chrono::steady_clock::now()-g_start).count();}
void Restart(){g_start=std::chrono::steady_clock::now();g_pausedTime=0;g_previewDirty=true;}
void Move(HWND h,int x,int y,int w,int hgt){MoveWindow(h,P(x),P(y),P(w),P(hgt),TRUE);}
void Move(int id,int x,int y,int w,int hgt){Move(C(id),x,y,w,hgt);}
void SetStatus(std::wstring text){g_status=std::move(text);InvalidateRect(g_window,nullptr,FALSE);}
Color RGBColor(uint32_t rgb,BYTE a=255){return Color(a,(rgb>>16)&255,(rgb>>8)&255,rgb&255);}

HWND Control(const wchar_t* cls,const wchar_t* caption,DWORD style,int id,HWND parent=nullptr,DWORD ex=0){
 HWND h=CreateWindowExW(ex,cls,caption,WS_CHILD|WS_VISIBLE|style,0,0,10,10,parent?parent:g_window,(HMENU)(INT_PTR)id,g_instance,nullptr);
 if(h){g_controls.push_back(h);SetWindowTheme(h,L"Explorer",nullptr);if(g_font)SendMessageW(h,WM_SETFONT,(WPARAM)g_font,TRUE);}return h;
}
HWND Button(const wchar_t* text,int id,HWND parent=nullptr){return Control(L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW,id,parent);}
HWND Edit(const wchar_t* text,int id,HWND parent=nullptr){auto h=Control(L"EDIT",text,WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,id,parent);SendMessageW(h,EM_SETLIMITTEXT,200,0);return h;}
void Slider(int id,int min,int max,int value){HWND h=Control(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,id);SendMessageW(h,TBM_SETRANGE,TRUE,MAKELPARAM(min,max));SendMessageW(h,TBM_SETPOS,TRUE,value);}

void RowsLayout(){
 if(!g_rows)return;
 RECT rc;GetClientRect(g_rows,&rc);
 int rowH=P(40),height=rc.bottom;
 int maxScroll=std::max(0,g_textCount*rowH-height);
 g_scroll=std::clamp(g_scroll,0,maxScroll);
 SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,g_textCount*rowH-1,(UINT)height,g_scroll};SetScrollInfo(g_rows,SB_VERT,&si,TRUE);
 int w=rc.right;
 for(int i=0;i<kMaxTexts;i++){
  bool visible=i<g_textCount;
  ShowWindow(g_edit[i],visible?SW_SHOW:SW_HIDE);ShowWindow(g_up[i],visible?SW_SHOW:SW_HIDE);ShowWindow(g_remove[i],visible?SW_SHOW:SW_HIDE);
  if(visible){int y=i*rowH-g_scroll;MoveWindow(g_edit[i],0,y,w-P(78),P(32),TRUE);MoveWindow(g_up[i],w-P(71),y,P(30),P(32),TRUE);MoveWindow(g_remove[i],w-P(36),y,P(30),P(32),TRUE);EnableWindow(g_up[i],i>0);EnableWindow(g_remove[i],g_textCount>1);}
 }
 EnableWindow(C(ID_ADD),g_textCount<kMaxTexts);
}
LRESULT CALLBACK RowsProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
 if(m==WM_COMMAND||m==WM_DRAWITEM||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORSTATIC)return SendMessageW(g_window,m,w,l);
 if(m==WM_SIZE){RowsLayout();return 0;}
 if(m==WM_MOUSEWHEEL){g_scroll-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*P(40);RowsLayout();return 0;}
 if(m==WM_VSCROLL){SCROLLINFO s{sizeof(s),SIF_ALL};GetScrollInfo(h,SB_VERT,&s);switch(LOWORD(w)){case SB_LINEUP:g_scroll-=P(40);break;case SB_LINEDOWN:g_scroll+=P(40);break;case SB_PAGEUP:g_scroll-=P(120);break;case SB_PAGEDOWN:g_scroll+=P(120);break;case SB_THUMBTRACK:g_scroll=s.nTrackPos;break;}RowsLayout();return 0;}
 return DefSubclassProc(h,m,w,l);
}

void LayoutControls(){
 RECT r;GetClientRect(g_window,&r);float scale=std::min(r.right/1100.0f,r.bottom/780.0f);g_scale=std::max(.65f,scale);
 HFONT old=g_font;g_font=CreateFontW(-P(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
 for(HWND h:g_controls)SendMessageW(h,WM_SETFONT,(WPARAM)g_font,TRUE);if(old)DeleteObject(old);
 Move(ID_ADD,346,116,66,28);Move(g_rows,44,154,368,132);
 Move(ID_FONT,44,371,254,260);SendMessageW(C(ID_FONT),CB_SETITEMHEIGHT,-1,P(26));Move(ID_SIZE,318,371,94,32);Move(ID_SIZE_SLIDER,42,409,372,29);
 Move(ID_FIT,44,445,214,26);Move(ID_BOLD,292,445,118,26);
 Move(ID_HOLD,44,539,164,32);Move(ID_TRANS,245,539,167,32);
 Move(ID_HOLD_SLIDER,40,583,174,28);Move(ID_TRANS_SLIDER,240,583,178,28);
 Move(ID_PAUSE,474,411,84,32);Move(ID_RESTART,568,411,84,32);
 Move(ID_WIDTH,474,538,68,31);Move(ID_HEIGHT,560,538,68,31);
 Move(ID_START_COLOR,695,538,47,31);Move(ID_END_COLOR,750,538,47,31);Move(ID_TEXT_COLOR,852,538,58,31);Move(ID_BORDER_COLOR,971,538,58,31);
 Move(ID_TRANSPARENT,474,592,178,26);Move(ID_LIMIT,695,592,202,26);Move(ID_BORDER_SIZE,973,592,56,29);
 Move(ID_OPEN,24,721,104,41);Move(ID_SAVE,140,721,104,41);Move(ID_RESET,257,721,104,41);Move(ID_EXPORT,868,715,208,49);
 RowsLayout();InvalidateRect(g_window,nullptr,TRUE);
}
int CALLBACK FontEnum(const LOGFONTW* lf,const TEXTMETRICW*,DWORD,LPARAM param){
 if(lf->lfFaceName[0]!=L'@'){HWND combo=(HWND)param;if(SendMessageW(combo,CB_FINDSTRINGEXACT,-1,(LPARAM)lf->lfFaceName)==CB_ERR)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)lf->lfFaceName);}return 1;
}
void MakeControls(){
 Button(L"＋ 添加",ID_ADD);
 g_rows=Control(L"STATIC",L"",WS_CLIPCHILDREN|WS_VSCROLL,90,nullptr,WS_EX_CONTROLPARENT);
 SetWindowSubclass(g_rows,RowsProc,1,0);
 for(int i=0;i<kMaxTexts;i++){g_edit[i]=Edit(L"",300+i,g_rows);g_up[i]=Button(L"↑",500+i,g_rows);g_remove[i]=Button(L"×",600+i,g_rows);}
 HWND combo=Control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL,ID_FONT);
 SendMessageW(combo,CB_SETMINVISIBLE,10,0);
 SendMessageW(combo,CB_LIMITTEXT,128,0);
 HDC dc=GetDC(g_window);LOGFONTW lf{};lf.lfCharSet=DEFAULT_CHARSET;EnumFontFamiliesExW(dc,&lf,FontEnum,(LPARAM)combo,0);ReleaseDC(g_window,dc);
 Edit(L"78",ID_SIZE);Slider(ID_SIZE_SLIDER,12,200,78);
 Control(L"BUTTON",L"文字太长时自动适配",WS_TABSTOP|BS_AUTOCHECKBOX,ID_FIT);
 Control(L"BUTTON",L"加粗",WS_TABSTOP|BS_AUTOCHECKBOX,ID_BOLD);
 Edit(L"2.0",ID_HOLD);Edit(L"0.6",ID_TRANS);Slider(ID_HOLD_SLIDER,5,100,20);Slider(ID_TRANS_SLIDER,1,30,6);
 Button(L"暂停",ID_PAUSE);Button(L"重新播放",ID_RESTART);
 Edit(L"480",ID_WIDTH);Edit(L"206",ID_HEIGHT);
 for(int id:{ID_START_COLOR,ID_END_COLOR,ID_TEXT_COLOR,ID_BORDER_COLOR})Button(L"",id);
 Control(L"BUTTON",L"透明背景",WS_TABSTOP|BS_AUTOCHECKBOX,ID_TRANSPARENT);
 Control(L"BUTTON",L"控制在 500 KB 以内",WS_TABSTOP|BS_AUTOCHECKBOX,ID_LIMIT);Check(ID_LIMIT,true);
 Edit(L"8",ID_BORDER_SIZE);
 Button(L"打开方案",ID_OPEN);Button(L"保存方案",ID_SAVE);Button(L"恢复默认",ID_RESET);Button(L"导出 GIF",ID_EXPORT);
}

bool Number(int id,double& value,double min,double max,const wchar_t* name,std::wstring& err){
 std::wstring s=Trim(Text(C(id)));wchar_t* end=nullptr;double d=wcstod(s.c_str(),&end);
 if(s.empty()||!end||*end||!std::isfinite(d)||d<min||d>max){err=std::wstring(name)+L"范围为 "+F(min,min==std::floor(min)?0:1)+L"–"+F(max,max==std::floor(max)?0:1)+L"。";return false;}
 value=d;return true;
}
bool ReadSettings(Settings& out,std::wstring& error){
 out=g_settings;out.texts.clear();
 for(int i=0;i<g_textCount;i++){std::wstring t=Trim(Text(g_edit[i]));if(!t.empty())out.texts.push_back(t);}
 if(out.texts.empty()){error=L"请至少填写一条文案。";return false;}
 out.fontFamily=Trim(Text(C(ID_FONT)));if(out.fontFamily.empty())out.fontFamily=L"Microsoft YaHei";
 double v=0;
 if(!Number(ID_SIZE,v,12,200,L"字号",error))return false;out.fontSize=(int)std::lround(v);
 if(!Number(ID_HOLD,v,.5,10,L"停留时长（秒）",error))return false;out.holdSeconds=std::round(v*100)/100;
 if(!Number(ID_TRANS,v,.1,3,L"变化时长（秒）",error))return false;out.transitionSeconds=std::round(v*100)/100;
 if(!Number(ID_WIDTH,v,160,960,L"宽度（像素）",error))return false;out.width=(int)std::lround(v);
 if(!Number(ID_HEIGHT,v,80,480,L"高度（像素）",error))return false;out.height=(int)std::lround(v);
 if(out.width*out.height>320000){error=L"请将图片总像素控制在 32 万以内。";return false;}
 if(!Number(ID_BORDER_SIZE,v,0,24,L"边框宽度",error))return false;out.borderWidth=(int)std::lround(v);
 out.autoFit=Checked(ID_FIT);out.bold=Checked(ID_BOLD);out.transparent=Checked(ID_TRANSPARENT);return true;
}
void Changed(){
 if(g_loading)return;
 Settings next;std::wstring err;
 if(ReadSettings(next,err)){
  g_settings=std::move(next);try{g_layout=InspectLayout(g_settings);g_validation=g_layout.warning;}catch(...){g_validation=L"该字体暂不可用，请换一个字体。";}
  SendMessageW(C(ID_SIZE_SLIDER),TBM_SETPOS,TRUE,g_settings.fontSize);
  SendMessageW(C(ID_HOLD_SLIDER),TBM_SETPOS,TRUE,(LPARAM)std::lround(g_settings.holdSeconds*10));
  SendMessageW(C(ID_TRANS_SLIDER),TBM_SETPOS,TRUE,(LPARAM)std::lround(g_settings.transitionSeconds*10));
  Restart();g_status=L"预览已更新。";
 }else g_validation=err;
 InvalidateRect(g_window,nullptr,FALSE);
}
void Apply(const Settings& s){
 g_loading=true;g_settings=s;g_textCount=std::clamp((int)s.texts.size(),1,kMaxTexts);
 for(int i=0;i<kMaxTexts;i++)SetWindowTextW(g_edit[i],i<(int)s.texts.size()?s.texts[i].c_str():L"");
 int fontIndex=(int)SendMessageW(C(ID_FONT),CB_FINDSTRINGEXACT,-1,(LPARAM)s.fontFamily.c_str());if(fontIndex==CB_ERR)fontIndex=(int)SendMessageW(C(ID_FONT),CB_ADDSTRING,0,(LPARAM)s.fontFamily.c_str());SendMessageW(C(ID_FONT),CB_SETCURSEL,fontIndex,0);
 SetText(ID_SIZE,std::to_wstring(s.fontSize));SetText(ID_HOLD,F(s.holdSeconds,2));SetText(ID_TRANS,F(s.transitionSeconds,2));
 SetText(ID_WIDTH,std::to_wstring(s.width));SetText(ID_HEIGHT,std::to_wstring(s.height));SetText(ID_BORDER_SIZE,std::to_wstring(s.borderWidth));
 Check(ID_FIT,s.autoFit);Check(ID_BOLD,s.bold);Check(ID_TRANSPARENT,s.transparent);
 g_scroll=0;RowsLayout();g_loading=false;Changed();
}

void Rounded(Graphics& g,RectF r,float radius,Color fill,Color border=Color(0,0,0,0)){
 GraphicsPath path;float d=radius*2;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();SolidBrush b(fill);g.FillPath(&b,&path);if(border.GetA()){Pen p(border,1);g.DrawPath(&p,&path);}
}
void Label(Graphics& g,const std::wstring& s,float x,float y,float w,float h,float size,Color color,bool bold=false){
 FontFamily family(L"Microsoft YaHei UI");Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(color);StringFormat f;f.SetFormatFlags(StringFormatFlagsNoWrap);f.SetTrimming(StringTrimmingEllipsisCharacter);f.SetLineAlignment(StringAlignmentCenter);g.DrawString(s.c_str(),-1,&font,RectF(x,y,w,h),&f,&b);
}
void UpdatePreview(){
 try{
  RenderedFrame f=RenderAt(g_settings,Now());g_preview.reset();g_previewWidth=f.width;g_previewHeight=f.height;g_previewPixels=std::move(f.rgba);
  for(size_t i=0;i<g_previewPixels.size();i+=4)std::swap(g_previewPixels[i],g_previewPixels[i+2]);
  g_preview=std::make_unique<Bitmap>(g_previewWidth,g_previewHeight,g_previewWidth*4,PixelFormat32bppARGB,g_previewPixels.data());g_previewDirty=false;
 }catch(...){g_validation=L"预览生成失败，请检查字体和图片尺寸。";}
}
void PaintToHdc(HDC dc){
 RECT rc;GetClientRect(g_window,&rc);
 Bitmap buffer(std::max(1L,rc.right),std::max(1L,rc.bottom),PixelFormat32bppARGB);Graphics g(&buffer);g.Clear(Color(255,247,247,244));g.SetSmoothingMode(SmoothingModeAntiAlias);g.ScaleTransform(g_scale,g_scale);
 Color ink(255,36,44,46),muted(255,112,120,121),accent(255,231,88,42),line(255,228,232,231);
 Rounded(g,RectF(24,24,46,46),13,accent);Label(g,L"动",34,28,40,37,26,Color::White,true);
 Label(g,L"轻动 GIF",84,23,360,35,27,ink,true);Label(g,L"填文案 · 调字号 · 自由控制动画节奏",86,59,640,25,13,muted);
 Rounded(g,RectF(939,34,137,31),15,Color(255,234,240,232));Label(g,L"离线使用 · 免安装",953,36,122,25,12,Color(255,74,111,76));
 Rounded(g,RectF(24,102,410,588),16,Color::White,line);Rounded(g,RectF(450,102,626,362),16,Color::White,line);Rounded(g,RectF(450,480,626,210),16,Color::White,line);
 Label(g,L"01  文案内容",44,116,270,28,16,ink,true);Label(g,L"按顺序轮播；↑ 上移，× 删除，空白条目跳过。",44,290,370,24,11,muted);
 Label(g,L"02  字体与字号",44,318,370,29,16,ink,true);Label(g,L"字体",44,348,240,19,11,muted);Label(g,L"字号 / px",318,348,94,19,11,muted);
 Label(g,L"03  动画节奏",44,477,368,29,16,ink,true);Label(g,L"停留时长 / 秒",44,510,170,25,13,ink);Label(g,L"动画变化时长 / 秒",245,510,170,25,13,ink);
 Label(g,L"文字完整显示多久",44,615,173,22,11,muted);Label(g,L"文字滚动切换多慢",245,615,173,22,11,muted);
 const double period=g_settings.holdSeconds+g_settings.transitionSeconds;
 Label(g,std::to_wstring(g_settings.texts.size())+L" 条文案 · 每轮 "+F(period*g_settings.texts.size(),2)+L" 秒",44,651,366,24,12,muted);
 Label(g,L"实时预览",474,119,300,30,17,ink,true);
 int current=g_settings.texts.empty()?0:(int)(Now()/period)%g_settings.texts.size()+1;
 Label(g,std::to_wstring(current)+L" / "+std::to_wstring(g_settings.texts.size()),996,119,57,30,13,muted);
 RectF previewRect(474,169,578,220);g.SetClip(previewRect);
 for(int y=169;y<390;y+=14)for(int x=474;x<1052;x+=14){SolidBrush b(((x-474)/14+(y-169)/14)%2?Color(255,236,239,238):Color(255,247,249,248));g.FillRectangle(&b,x,y,14,14);}
 if(g_previewDirty||(!g_paused&&!g_exporting))UpdatePreview();
 if(g_preview){float fit=std::min(578.0f/g_previewWidth,220.0f/g_previewHeight);float w=g_previewWidth*fit,h=g_previewHeight*fit;g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(g_preview.get(),RectF(474+(578-w)/2,169+(220-h)/2,w,h));}
 g.ResetClip();
 Label(g,std::to_wstring(g_settings.width)+L" × "+std::to_wstring(g_settings.height)+L" px    实际字号 "+std::to_wstring(g_layout.effectiveFontSize)+L" px",674,413,381,28,12,muted);
 Label(g,L"导出设置",474,491,470,29,16,ink,true);
 Label(g,L"尺寸 / px",474,517,170,20,11,muted);Label(g,L"×",545,537,20,31,15,muted);
 Label(g,L"按钮渐变",695,517,108,20,11,muted);Label(g,L"文字颜色",852,517,90,20,11,muted);Label(g,L"边框颜色",971,517,81,20,11,muted);
 Label(g,L"边框 / px",932,573,99,18,10,muted);
 Label(g,L"停留加长只增加阅读时间；变化加长让滚动更慢。",474,640,566,27,12,muted);
 std::wstring message=!g_validation.empty()?g_validation:g_status;
 Label(g,message,387,714,461,49,12,!g_validation.empty()?Color(255,178,85,30):muted);
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);
}
void Paint(){PAINTSTRUCT ps;HDC dc=BeginPaint(g_window,&ps);PaintToHdc(dc);EndPaint(g_window,&ps);}
void DrawButton(DRAWITEMSTRUCT* d){
 if(!d)return;Graphics g(d->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);
 RECT r=d->rcItem;RectF rr((float)r.left+1,(float)r.top+1,(float)(r.right-r.left-2),(float)(r.bottom-r.top-2));
 bool down=(d->itemState&ODS_SELECTED)!=0,disabled=(d->itemState&ODS_DISABLED)!=0;
 Color fill=Color(255,246,247,246),edge(255,225,230,227),ink(255,56,65,63);
 int id=d->CtlID;
 if(id==ID_EXPORT){fill=down?Color(255,202,69,26):Color(255,231,88,42);edge=fill;ink=Color::White;}
 else if(down)fill=Color(255,228,234,230);
 if(disabled){fill=Color(255,239,241,239);ink=Color(255,174,180,175);}
 uint32_t rgb=0;bool swatch=true;
 switch(id){case ID_START_COLOR:rgb=g_settings.gradientStart;break;case ID_END_COLOR:rgb=g_settings.gradientEnd;break;case ID_TEXT_COLOR:rgb=g_settings.textColor;break;case ID_BORDER_COLOR:rgb=g_settings.borderColor;break;default:swatch=false;}
 if(swatch)fill=RGBColor(rgb);
 Rounded(g,rr,6*g_scale,fill,edge);
 std::wstring text=Text(d->hwndItem);FontFamily family(L"Microsoft YaHei UI");Font font(&family,(id==ID_EXPORT?16.0f:13.0f)*g_scale,id==ID_EXPORT?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(ink);StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentCenter);g.DrawString(text.c_str(),-1,&font,rr,&sf,&b);
 if(d->itemState&ODS_FOCUS){RECT focus=r;InflateRect(&focus,-4,-4);DrawFocusRect(d->hDC,&focus);}
}

std::wstring FileDialog(bool save,bool gif){
 wchar_t filename[32768]=L"";if(save)wcscpy_s(filename,gif?L"文字轮播.gif":L"我的动图方案.gifcfg");
 OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=g_window;ofn.lpstrFile=filename;ofn.nMaxFile=32768;
 ofn.lpstrFilter=gif?L"GIF 动图 (*.gif)\0*.gif\0\0":L"轻动 GIF 方案 (*.gifcfg)\0*.gifcfg\0\0";
 ofn.lpstrDefExt=gif?L"gif":L"gifcfg";ofn.lpstrTitle=gif?L"导出 GIF 动图":save?L"保存当前方案":L"打开动图方案";
 ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
 BOOL ok=save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn);return ok?filename:L"";
}
void WriteAll(const std::wstring& filename,const std::vector<unsigned char>& bytes){
 std::wstring temp=filename+L".writing-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create output file");
 DWORD written=0;BOOL ok=WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr);if(ok)ok=FlushFileBuffers(file);CloseHandle(file);
 if(!ok||written!=bytes.size()){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot write output file");}
 if(!MoveFileExW(temp.c_str(),filename.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot replace output file");}
}
std::vector<GifFrame> Frames(const Settings& s,int fps,std::atomic<bool>* cancel=nullptr){
 std::vector<GifFrame> result;
 int holdCs=std::max(1,(int)std::lround(s.holdSeconds*100)),transCs=std::max(1,(int)std::lround(s.transitionSeconds*100));
 int frameBudget=std::max(24,std::min(200,(int)(96ull*1024*1024/(s.width*s.height*4))));
 int steps=std::clamp((int)std::ceil(s.transitionSeconds*fps),1,std::max(1,frameBudget/(int)s.texts.size()-1));steps=std::min(steps,transCs);
 for(size_t i=0;i<s.texts.size();i++){
  if(cancel&&cancel->load())throw std::runtime_error("Cancelled");
  double base=i*(s.holdSeconds+s.transitionSeconds);
  RenderedFrame f=RenderAt(s,base+.00000001);result.push_back({f.width,f.height,holdCs,std::move(f.rgba)});
  if(s.texts.size()==1){result.back().delayCs+=transCs;continue;}
  for(int j=0;j<steps;j++){
   if(cancel&&cancel->load())throw std::runtime_error("Cancelled");
   double t=base+s.holdSeconds+s.transitionSeconds*(j+.5)/steps;
   f=RenderAt(s,t);int delay=(j+1)*transCs/steps-j*transCs/steps;
   result.push_back({f.width,f.height,delay,std::move(f.rgba)});
  }
 }
 return result;
}
std::vector<unsigned char> ExportBytes(const Settings& s,bool limited,std::function<void(const std::wstring&)> progress={},std::atomic<bool>* cancel=nullptr){
 for(int fps:{25,20,15,12,8}){
  if(progress)progress(L"正在生成动画帧…");
  auto frames=Frames(s,fps,cancel);
  const std::vector<int> palettes=fps>=15?std::vector<int>{256,128}:(fps==12?std::vector<int>{128,64}:std::vector<int>{64,32});
  for(int colors:palettes){
   if(cancel&&cancel->load())throw std::runtime_error("Cancelled");
   if(progress)progress(L"正在优化 GIF，保持字号与时长…");
   GifOptions options;options.maxColors=colors;auto bytes=EncodeGif(frames,options);
   if(!limited||bytes.size()<500000)return bytes;
  }
 }
 throw std::runtime_error("Size limit");
}
struct ExportResult {bool ok=false;bool cancelled=false;size_t bytes=0;std::wstring filename,error;};
void BeginExport(){
 if(g_exporting)return;Settings s;std::wstring error;
 if(!ReadSettings(s,error)){MessageBoxW(g_window,error.c_str(),L"请检查设置",MB_OK|MB_ICONINFORMATION);return;}
 auto layout=InspectLayout(s);if(!s.autoFit&&!layout.fits){MessageBoxW(g_window,L"当前字号会让文字超出按钮。请调小字号，或打开“文字太长时自动适配”。",L"文字放不下",MB_OK|MB_ICONINFORMATION);return;}
 std::wstring filename=FileDialog(true,true);if(filename.empty())return;
 if(g_worker.joinable())g_worker.join();bool limited=Checked(ID_LIMIT);g_exporting=true;g_cancel=false;EnableWindow(C(ID_EXPORT),FALSE);SetText(ID_EXPORT,L"正在导出…");SetStatus(L"正在生成 GIF，请稍候…");
 g_worker=std::thread([s,limited,filename]{
  auto result=new ExportResult;result->filename=filename;
  try{auto bytes=ExportBytes(s,limited,[](const std::wstring& message){auto p=new std::wstring(message);if(!PostMessageW(g_window,WM_EXPORT_PROGRESS,0,(LPARAM)p))delete p;},&g_cancel);
   if(g_cancel)result->cancelled=true;else{WriteAll(filename,bytes);result->ok=true;result->bytes=bytes.size();}
  }catch(const std::exception& e){std::string what=e.what();if(what=="Cancelled")result->cancelled=true;else if(what=="Size limit")result->error=L"当前尺寸和文案数量无法压缩到 500 KB 以下。请缩小图片尺寸、减少条目，或取消体积限制后导出。";else result->error=L"导出失败。请检查保存位置是否可写、文件是否被占用，或尝试更换字体后重试。";}
  catch(...){result->error=L"导出失败，请调整设置后重试。";}
  if(!PostMessageW(g_window,WM_EXPORT_DONE,0,(LPARAM)result))delete result;
 });
}

std::wstring HexEncode(const std::wstring& value){std::wostringstream o;for(wchar_t c:value)o<<std::hex<<std::setw(4)<<std::setfill(L'0')<<(unsigned int)(unsigned short)c;return o.str();}
std::wstring HexDecode(const std::wstring& value){if(value.size()%4)throw std::runtime_error("Invalid preset string");std::wstring out;for(size_t i=0;i<value.size();i+=4){auto token=value.substr(i,4);wchar_t* end=nullptr;auto code=wcstoul(token.c_str(),&end,16);if(!end||*end||code==0||code>65535)throw std::runtime_error("Invalid preset string");out.push_back((wchar_t)code);}return out;}
void SavePreset(const std::wstring& path,const Settings& s,bool limited=true){
 std::wostringstream content;content<<L"[LightGif]\r\nVersion=2\r\nCount="<<s.texts.size()<<L"\r\n";
 for(size_t i=0;i<s.texts.size();i++)content<<L"Text"<<i<<L"="<<HexEncode(s.texts[i])<<L"\r\n";
 content<<L"Font="<<HexEncode(s.fontFamily)<<L"\r\nFontSize="<<s.fontSize<<L"\r\nHold="<<F(s.holdSeconds,2)<<L"\r\nTransition="<<F(s.transitionSeconds,2)<<L"\r\nWidth="<<s.width<<L"\r\nHeight="<<s.height<<L"\r\nBorderWidth="<<s.borderWidth<<L"\r\nAutoFit="<<s.autoFit<<L"\r\nBold="<<s.bold<<L"\r\nTransparent="<<s.transparent<<L"\r\nLimit="<<limited<<L"\r\nStart="<<s.gradientStart<<L"\r\nEnd="<<s.gradientEnd<<L"\r\nTextColor="<<s.textColor<<L"\r\nBorderColor="<<s.borderColor<<L"\r\n";
 std::wstring value=L"\ufeff"+content.str();std::vector<unsigned char> bytes(value.size()*sizeof(wchar_t));memcpy(bytes.data(),value.data(),bytes.size());WriteAll(path,bytes);
}
Settings LoadPreset(const std::wstring& path,bool* limited=nullptr){
 auto get=[&](const std::wstring& key,const std::wstring& def){wchar_t b[2048];GetPrivateProfileStringW(L"LightGif",key.c_str(),def.c_str(),b,2048,path.c_str());return std::wstring(b);};
 std::wstring version=get(L"Version",L"");if(version!=L"1"&&version!=L"2")throw std::runtime_error("Invalid preset");Settings s;
 int count=std::clamp(_wtoi(get(L"Count",L"3").c_str()),1,kMaxTexts);s.texts.clear();
 for(int i=0;i<count;i++){auto t=get(L"Text"+std::to_wstring(i),L"");if(version==L"2")t=HexDecode(t);t=Trim(t);if(!t.empty())s.texts.push_back(t);}if(s.texts.empty())throw std::runtime_error("Empty preset");
 s.fontFamily=get(L"Font",version==L"2"?HexEncode(s.fontFamily):s.fontFamily);if(version==L"2")s.fontFamily=HexDecode(s.fontFamily);if(s.fontFamily.size()>128)throw std::runtime_error("Invalid font name");s.fontSize=std::clamp(_wtoi(get(L"FontSize",L"78").c_str()),12,200);
 double hold=_wtof(get(L"Hold",L"2").c_str()),transition=_wtof(get(L"Transition",L".6").c_str());
 if(!std::isfinite(hold)||!std::isfinite(transition))throw std::runtime_error("Invalid preset timing");
 s.holdSeconds=std::clamp(hold,.5,10.);s.transitionSeconds=std::clamp(transition,.1,3.);
 s.width=std::clamp(_wtoi(get(L"Width",L"480").c_str()),160,960);s.height=std::clamp(_wtoi(get(L"Height",L"206").c_str()),80,480);if(s.width*s.height>320000)throw std::runtime_error("Too large preset");
 s.borderWidth=std::clamp(_wtoi(get(L"BorderWidth",L"8").c_str()),0,24);s.autoFit=get(L"AutoFit",L"1")!=L"0";s.bold=get(L"Bold",L"0")==L"1";s.transparent=get(L"Transparent",L"1")!=L"0";
 auto color=[&](const wchar_t* key,uint32_t def){return (uint32_t)_wcstoui64(get(key,std::to_wstring(def)).c_str(),nullptr,10)&0xFFFFFF;};
 s.gradientStart=color(L"Start",s.gradientStart);s.gradientEnd=color(L"End",s.gradientEnd);s.textColor=color(L"TextColor",s.textColor);s.borderColor=color(L"BorderColor",s.borderColor);if(limited)*limited=get(L"Limit",L"1")!=L"0";return s;
}
void PresetCommand(bool save){
 std::wstring file=FileDialog(save,false);if(file.empty())return;
 try{if(save){Settings s;std::wstring error;if(!ReadSettings(s,error))throw std::runtime_error("Invalid settings");SavePreset(file,s,Checked(ID_LIMIT));SetStatus(L"方案已保存，下次可直接打开。 ");}else{bool limited=true;Apply(LoadPreset(file,&limited));Check(ID_LIMIT,limited);SetStatus(L"已加载方案。 ");}}
 catch(...){MessageBoxW(g_window,save?L"方案保存失败，请检查设置和保存位置。":L"无法读取这个方案文件，请选择本软件保存的 .gifcfg 文件。",L"轻动 GIF",MB_OK|MB_ICONINFORMATION);}
}
void Choose(int id){
 uint32_t* target=nullptr;switch(id){case ID_START_COLOR:target=&g_settings.gradientStart;break;case ID_END_COLOR:target=&g_settings.gradientEnd;break;case ID_TEXT_COLOR:target=&g_settings.textColor;break;case ID_BORDER_COLOR:target=&g_settings.borderColor;break;}
 if(!target)return;CHOOSECOLORW c{sizeof(c)};c.hwndOwner=g_window;c.rgbResult=RGB((*target>>16)&255,(*target>>8)&255,*target&255);c.lpCustColors=g_customColors;c.Flags=CC_FULLOPEN|CC_RGBINIT;
 if(ChooseColorW(&c)){*target=(GetRValue(c.rgbResult)<<16)|(GetGValue(c.rgbResult)<<8)|GetBValue(c.rgbResult);Changed();InvalidateRect(C(id),nullptr,TRUE);}
}
void Reorder(int index){if(index<=0||index>=g_textCount)return;g_loading=true;auto a=Text(g_edit[index-1]),b=Text(g_edit[index]);SetWindowTextW(g_edit[index-1],b.c_str());SetWindowTextW(g_edit[index],a.c_str());g_loading=false;Changed();if(IsWindowVisible(g_window))SetFocus(g_edit[index-1]);}
void Remove(int index){if(g_textCount<=1||index>=g_textCount)return;g_loading=true;for(int i=index;i<g_textCount-1;i++)SetWindowTextW(g_edit[i],Text(g_edit[i+1]).c_str());SetWindowTextW(g_edit[--g_textCount],L"");g_loading=false;RowsLayout();Changed();}

LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){
 switch(m){
 case WM_CREATE:g_window=h;g_white=CreateSolidBrush(RGB(255,255,255));MakeControls();Apply(Settings{});Restart();SetTimer(h,1,40,nullptr);return 0;
 case WM_SIZE:LayoutControls();return 0;
 case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)l;p->ptMinTrackSize={800,590};return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:Paint();return 0;
 case WM_PRINTCLIENT:PaintToHdc((HDC)w);return 0;
 case WM_DRAWITEM:DrawButton((DRAWITEMSTRUCT*)l);return TRUE;
 case WM_CTLCOLOREDIT:SetTextColor((HDC)w,RGB(44,52,51));SetBkColor((HDC)w,RGB(255,255,255));return (LRESULT)g_white;
 case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetTextColor((HDC)w,RGB(56,65,63));SetBkColor((HDC)w,RGB(255,255,255));return (LRESULT)g_white;
 case WM_TIMER:if(w==2){PostMessageW(h,WM_CLOSE,0,0);return 0;}if(!g_paused&&!g_exporting){RECT r{P(465),P(115),P(1058),P(396)};InvalidateRect(h,&r,FALSE);}return 0;
 case WM_HSCROLL:{int id=GetDlgCtrlID((HWND)l);int v=(int)SendMessageW((HWND)l,TBM_GETPOS,0,0);g_loading=true;if(id==ID_SIZE_SLIDER)SetText(ID_SIZE,std::to_wstring(v));if(id==ID_HOLD_SLIDER)SetText(ID_HOLD,F(v/10.0));if(id==ID_TRANS_SLIDER)SetText(ID_TRANS,F(v/10.0));g_loading=false;Changed();return 0;}
 case WM_COMMAND:{int id=LOWORD(w),code=HIWORD(w);
  if(id>=500&&id<500+kMaxTexts&&code==BN_CLICKED){Reorder(id-500);return 0;}if(id>=600&&id<600+kMaxTexts&&code==BN_CLICKED){Remove(id-600);return 0;}
  if(code==EN_CHANGE||code==CBN_EDITCHANGE||code==CBN_SELCHANGE){
   if(g_loading)return 0;
   if(code==CBN_SELCHANGE&&id==ID_FONT){int index=(int)SendMessageW(C(ID_FONT),CB_GETCURSEL,0,0);if(index>=0){int count=(int)SendMessageW(C(ID_FONT),CB_GETLBTEXTLEN,index,0);std::wstring name(count+1,L'\0');SendMessageW(C(ID_FONT),CB_GETLBTEXT,index,(LPARAM)name.data());COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(C(ID_FONT),&info)&&info.hwndItem){g_loading=true;SetWindowTextW(info.hwndItem,name.c_str());g_loading=false;}}}
   Changed();return 0;}
  if(code!=BN_CLICKED)break;
  switch(id){
  case ID_ADD:if(g_textCount<kMaxTexts){g_loading=true;SetWindowTextW(g_edit[g_textCount++],L"新文案");g_loading=false;g_scroll=P(40)*g_textCount;RowsLayout();Changed();if(IsWindowVisible(g_window))SetFocus(g_edit[g_textCount-1]);}return 0;
  case ID_FIT:case ID_BOLD:case ID_TRANSPARENT:Changed();return 0;
  case ID_PAUSE:if(g_paused){g_start=std::chrono::steady_clock::now()-std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(g_pausedTime));g_paused=false;SetText(ID_PAUSE,L"暂停");}else{g_pausedTime=Now();g_paused=true;SetText(ID_PAUSE,L"播放");}InvalidateRect(h,nullptr,FALSE);return 0;
  case ID_RESTART:g_paused=false;SetText(ID_PAUSE,L"暂停");Restart();InvalidateRect(h,nullptr,FALSE);return 0;
  case ID_EXPORT:BeginExport();return 0;
  case ID_OPEN:PresetCommand(false);return 0;case ID_SAVE:PresetCommand(true);return 0;case ID_RESET:Apply(Settings{});Check(ID_LIMIT,true);SetStatus(L"已恢复默认设置。 ");return 0;
  case ID_START_COLOR:case ID_END_COLOR:case ID_TEXT_COLOR:case ID_BORDER_COLOR:Choose(id);return 0;
  }
  break;}
 case WM_EXPORT_PROGRESS:{std::unique_ptr<std::wstring> message((std::wstring*)l);if(g_exporting)SetStatus(*message);return 0;}
 case WM_EXPORT_DONE:{std::unique_ptr<ExportResult> result((ExportResult*)l);if(g_worker.joinable())g_worker.join();g_exporting=false;EnableWindow(C(ID_EXPORT),TRUE);SetText(ID_EXPORT,L"导出 GIF");
  if(result->ok){SetStatus(L"导出成功 · "+F(result->bytes/1000.0,1)+L" KB");std::wstring message=L"GIF 已保存。\n\n文件大小："+F(result->bytes/1000.0,1)+L" KB\n保存位置："+result->filename;MessageBoxW(h,message.c_str(),L"导出成功",MB_OK|MB_ICONINFORMATION);}else if(!result->cancelled){SetStatus(L"导出未完成，请调整后重试。");MessageBoxW(h,result->error.c_str(),L"导出未完成",MB_OK|MB_ICONINFORMATION);}return 0;}
 case WM_CLOSE:g_cancel=true;if(g_worker.joinable())g_worker.join();DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,1);if(g_font)DeleteObject(g_font);if(g_white)DeleteObject(g_white);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(h,m,w,l);
}

int SelfTest(const std::wstring& dir){
 CreateDirectoryW(dir.c_str(),nullptr);std::ofstream report((dir+L"/selftest.txt").c_str(),std::ios::binary);
 try{
  std::vector<std::pair<std::wstring,Settings>> cases;
  Settings s;cases.push_back({L"default",s});s.holdSeconds=4;cases.push_back({L"long-hold",s});s=Settings{};s.transitionSeconds=1.2;cases.push_back({L"slow-transition",s});
  s=Settings{};s.texts={L"专业门窗安装维修",L"防水补漏 Waterproof",L"翻新改造"};s.fontSize=120;cases.push_back({L"long-text",s});
  s=Settings{};s.texts={L"门窗维修"};cases.push_back({L"single",s});
  s=Settings{};s.transparent=false;s.gradientStart=0x126C91;s.gradientEnd=0x39BD9C;cases.push_back({L"opaque-color",s});
  for(auto& item:cases){auto start=std::chrono::steady_clock::now();auto bytes=ExportBytes(item.second,true);WriteAll(dir+L"/"+item.first+L".gif",bytes);auto layout=InspectLayout(item.second);report<<std::string(item.first.begin(),item.first.end())<<" bytes="<<bytes.size()<<" effective_font="<<layout.effectiveFontSize<<" duration_cs="<<std::lround((item.second.holdSeconds+item.second.transitionSeconds)*100)*item.second.texts.size()<<" milliseconds="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"\n";}
  s=Settings{};s.texts={L"含中文的方案=测试",L"\"防水补漏\"",L"翻新改造"};SavePreset(dir+L"/中文方案.gifcfg",s,false);bool limited=true;auto loaded=LoadPreset(dir+L"/中文方案.gifcfg",&limited);if(loaded.texts!=s.texts||loaded.fontSize!=s.fontSize||loaded.holdSeconds!=s.holdSeconds||loaded.gradientStart!=s.gradientStart||limited)throw std::runtime_error("Preset roundtrip");
  SavePreset(dir+L"/默认方案.gifcfg",Settings{});
  auto begin=std::chrono::steady_clock::now();for(int i=0;i<20;i++)RenderAt(Settings{},i*.03);report<<"preview_average_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/20<<"\nPASS\n";return 0;
 }catch(const std::exception& e){report<<"FAIL "<<e.what()<<"\n";return 2;}catch(...){report<<"FAIL unknown\n";return 3;}
}

bool UiSnapshot(const std::wstring& path){
 // Render this program's own hidden window for build QA; never read the desktop.
 Settings observed;std::wstring error;Settings initial;if(!ReadSettings(observed,error)||observed.fontFamily!=initial.fontFamily||observed.texts!=initial.texts||observed.fontSize!=initial.fontSize||observed.holdSeconds!=initial.holdSeconds||observed.transitionSeconds!=initial.transitionSeconds)return false;
 // Widget/model integration checks run entirely in this hidden application.
 SetText(ID_HOLD,L"4.0");if(g_settings.holdSeconds!=4.0||g_settings.transitionSeconds!=.6)return false;
 SetText(ID_TRANS,L"1.2");if(g_settings.holdSeconds!=4.0||g_settings.transitionSeconds!=1.2)return false;
 SetText(ID_SIZE,L"64");if(g_settings.fontSize!=64)return false;
 SendMessageW(C(ID_SIZE_SLIDER),TBM_SETPOS,TRUE,90);WindowProc(g_window,WM_HSCROLL,TB_THUMBPOSITION,(LPARAM)C(ID_SIZE_SLIDER));if(g_settings.fontSize!=90||Text(C(ID_SIZE))!=L"90")return false;
 SetWindowTextW(g_edit[0],L"测试文案 ABC");if(g_settings.texts[0]!=L"测试文案 ABC")return false;
 int fontIndex=(int)SendMessageW(C(ID_FONT),CB_FINDSTRINGEXACT,-1,(LPARAM)L"Arial");if(fontIndex==CB_ERR)fontIndex=(int)SendMessageW(C(ID_FONT),CB_ADDSTRING,0,(LPARAM)L"Arial");SendMessageW(C(ID_FONT),CB_SETCURSEL,fontIndex,0);WindowProc(g_window,WM_COMMAND,MAKEWPARAM(ID_FONT,CBN_SELCHANGE),(LPARAM)C(ID_FONT));if(g_settings.fontFamily!=L"Arial")return false;
 WindowProc(g_window,WM_COMMAND,MAKEWPARAM(ID_ADD,BN_CLICKED),0);if(g_textCount!=4||g_settings.texts.size()!=4)return false;Remove(3);if(g_textCount!=3)return false;Reorder(1);if(g_settings.texts[1]!=L"测试文案 ABC")return false;
 WindowProc(g_window,WM_COMMAND,MAKEWPARAM(ID_PAUSE,BN_CLICKED),0);if(!g_paused)return false;WindowProc(g_window,WM_COMMAND,MAKEWPARAM(ID_RESTART,BN_CLICKED),0);if(g_paused)return false;
 Apply(initial);g_status=L"准备就绪，调整后即可导出。";
 g_paused=true;g_pausedTime=0;g_previewDirty=true;LayoutControls();
 RECT r;GetClientRect(g_window,&r);HDC dc=GetDC(g_window);HDC memory=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);HGDIOBJ old=SelectObject(memory,bitmap);
 PaintToHdc(memory);
 for(HWND child:g_controls){
  if(GetParent(child)!=g_window||!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
  RECT cr;GetWindowRect(child,&cr);MapWindowPoints(nullptr,g_window,(POINT*)&cr,2);int width=cr.right-cr.left,height=cr.bottom-cr.top;if(width<=0||height<=0)continue;
  HDC childDc=CreateCompatibleDC(dc);HBITMAP childBitmap=CreateCompatibleBitmap(dc,width,height);HGDIOBJ childOld=SelectObject(childDc,childBitmap);
  BitBlt(childDc,0,0,width,height,memory,cr.left,cr.top,SRCCOPY);
  SendMessageW(child,WM_PRINT,(WPARAM)childDc,PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
  BitBlt(memory,cr.left,cr.top,width,height,childDc,0,0,SRCCOPY);
  SelectObject(childDc,childOld);DeleteObject(childBitmap);DeleteDC(childDc);
 }
 bool ok=false;
 {Bitmap output(bitmap,nullptr);UINT size=0,count=0;GetImageEncodersSize(&count,&size);std::vector<unsigned char> bytes(size);auto info=(ImageCodecInfo*)bytes.data();if(size&&GetImageEncoders(count,size,info)==Ok){for(UINT i=0;i<count;i++)if(wcscmp(info[i].MimeType,L"image/png")==0){ok=output.Save(path.c_str(),&info[i].Clsid,nullptr)==Ok;break;}}}
 SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(g_window,dc);return ok;
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
 g_instance=instance;GdiplusStartupInput input;ULONG_PTR token=0;if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 1;
 int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);int result=0;
 bool backgroundPreview=argc>=2&&std::wstring(argv[1])==L"--background-preview";
 std::wstring uiSnapshotPath=argc>=3&&std::wstring(argv[1])==L"--ui-snapshot"?argv[2]:L"";
 if(argc>=3&&std::wstring(argv[1])==L"--self-test"){result=SelfTest(argv[2]);LocalFree(argv);GdiplusShutdown(token);return result;}
 LocalFree(argv);
 SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
 WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"LightGifStudioWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));wc.hIconSm=wc.hIcon;if(!wc.hIcon)wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
 RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);HDC dc=GetDC(nullptr);double dpi=GetDeviceCaps(dc,LOGPIXELSX)/96.;ReleaseDC(nullptr,dc);
 double scale=std::min(dpi,std::min((area.right-area.left-40)/1100.,(area.bottom-area.top-60)/780.));RECT size{0,0,(LONG)std::lround(1100*scale),(LONG)std::lround(780*scale)};AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_APPWINDOW);
 HWND window=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"轻动 GIF · 文字动图制作器",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,area.left+(area.right-area.left-(size.right-size.left))/2,area.top+(area.bottom-area.top-(size.bottom-size.top))/2,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
 if(!window){GdiplusShutdown(token);return 1;}
 if(!uiSnapshotPath.empty()){bool ok=UiSnapshot(uiSnapshotPath);DestroyWindow(window);g_preview.reset();g_previewPixels.clear();GdiplusShutdown(token);return ok?0:4;}
 if(backgroundPreview){SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);SetWindowPos(window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);SetTimer(window,2,120000,nullptr);}else ShowWindow(window,show);UpdateWindow(window);MSG message;
 while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
 g_preview.reset();g_previewPixels.clear();GdiplusShutdown(token);return (int)message.wParam;
}
