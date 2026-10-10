#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <Windows.h>

namespace {

constexpr int kCheckboxId = 1001;
constexpr int kSaveButtonId = 1002;
constexpr int kMyanglishEditId = 1101;
constexpr int kMyanmarEditId = 1102;
constexpr int kAddWordButtonId = 1103;

constexpr int kAboutButtonId = 1201;
constexpr int kStoryButtonId = 1202;
constexpr int kReaderEditId = 1203;
constexpr wchar_t kAboutText[] = LR"MYABOUT(# Myanglish အကြောင်း

မြန်မာစာရိုက်ဖို့ လက်ကွက်ကျက်စရာ မလိုတော့ဘူး။

Myanglish သည် မြန်မာစာကို ပိုမိုလွယ်ကူ၊ မြန်ဆန်ပြီး သဘာဝကျကျ ရိုက်နိုင်စေရန် ဖန်တီးထားသော Windows မြန်မာစာရိုက်စနစ် (IME) တစ်ခုဖြစ်ပါသည်။

ဤ Project ကို စတင်ခဲ့ရသည့် ရည်ရွယ်ချက်မှာ ရိုးရှင်းပါသည်။ မြန်မာ Keyboard လက်ကွက်ကို သီးသန့်ကျက်မှတ်ရန် မလိုဘဲ မိမိရင်းနှီးပြီးသား English အက္ခရာများဖြင့် အသံထွက်အတိုင်း ရိုက်နှိပ်ကာ မြန်မာစာအဖြစ် ပြောင်းလဲနိုင်စေရန် ဖြစ်ပါသည်။

အစပိုင်းတွင် ရိုးရှင်းသော စာသားပြောင်းလဲသည့် Program တစ်ခုအဖြစ် စတင်ခဲ့သော်လည်း အကြိမ်ကြိမ် စမ်းသပ်ခြင်း၊ အမှားများကို ရှာဖွေပြင်ဆင်ခြင်းနှင့် လက်တွေ့အသုံးပြုမှုများမှတစ်ဆင့် Windows Application အမျိုးမျိုးတွင် အသုံးပြုနိုင်သော IME တစ်ခုအဖြစ် တဖြည်းဖြည်း တိုးတက်လာခဲ့ပါသည်။

ဖန်တီးမှုလမ်းကြောင်းတစ်လျှောက် Windows Application များနှင့် ကိုက်ညီမှု၊ မြန်မာ Unicode စနစ်၊ စာရိုက်သည့်နေရာ မှန်ကန်မှု၊ စကားလုံးရွေးချယ်မှုနှင့် ရှုပ်ထွေးသော မြန်မာဆင့်စာလုံးများကဲ့သို့ နည်းပညာဆိုင်ရာ အခက်အခဲများစွာကို ရင်ဆိုင်ခဲ့ရပါသည်။

အခက်အခဲတစ်ခုချင်းစီကို စမ်းသပ်၊ လေ့လာ၊ ပြန်လည်ပြင်ဆင်ရင်း Myanglish ကို ပိုမိုကောင်းမွန်အောင် ဆက်လက်တည်ဆောက်ခဲ့ပါသည်။

Myanglish သည် ကျွန်တော့်အတွက် Software Project တစ်ခုထက် ပိုပါသည်။

၎င်းသည် စိတ်ကူးတစ်ခုကို လက်တွေ့အကောင်အထည်ဖော်ခြင်း၊ ပြဿနာများကို ကိုယ်တိုင်ဖြေရှင်းခြင်း၊ အမှားများမှ သင်ယူခြင်းနှင့် အခက်အခဲများကြားမှ လက်မလျှော့ဘဲ ဆက်လက်ကြိုးစားခဲ့ခြင်းတို့၏ ရလဒ်တစ်ခုဖြစ်ပါသည်။

ကျွန်တော့်၏ ရည်မှန်းချက်မှာ ရိုးရှင်းပါသည်။

“လူတိုင်း မြန်မာစာကို လွယ်ကူပြီး သဘာဝကျကျ ရိုက်နိုင်စေရန်။”

---

Myanglish — by MAUNG KAUNG

စိတ်ကူးတစ်ခုမှ စတင်ခဲ့ပြီး အခက်အခဲများကို ကျော်ဖြတ်ကာ အမြဲတမ်း ပိုမိုကောင်းမွန်အောင် ဆက်လက်ဖန်တီးနေသော မြန်မာစာရိုက်စနစ်။)MYABOUT";
constexpr wchar_t kStoryText[] = LR"MYSTORY(# My Story
စိတ်ကူးတစ်ခုမှ Myanglish IME ဖြစ်လာသည်အထိ

မြန်မာစာရိုက်ဖို့ လက်ကွက်ကျက်စရာ မလိုတော့ဘူး။

Myanglish IME ကို ဖန်တီးဖို့ စတင်စဉ်းစားခဲ့တဲ့အချိန်မှာ ကျွန်တော့်မှာ ရိုးရှင်းတဲ့ ရည်ရွယ်ချက်တစ်ခုပဲ ရှိခဲ့ပါတယ်။ မြန်မာစာကို လူတိုင်း ပိုမိုလွယ်ကူစွာ ရိုက်နိုင်စေချင်တာပါ။

မြန်မာ Keyboard ကို အသုံးပြုတဲ့အခါ စာလုံးတွေရဲ့ တည်နေရာကို မှတ်သားရတာ၊ သီးသန့်လက်ကွက်လေ့လာရတာနဲ့ ဆင့်စာလုံးတွေကို ရိုက်ဖို့ Key အများကြီး မှတ်ထားရတာတွေ ရှိပါတယ်။ ဒါကြောင့် English Keyboard တစ်ခုတည်းကို အသုံးပြုပြီး မြန်မာစကားသံထွက်အတိုင်း ရိုက်လိုက်ရင် မြန်မာစာအဖြစ် ပြောင်းပေးနိုင်တဲ့ စနစ်တစ်ခု ဖန်တီးဖို့ စဉ်းစားခဲ့ပါတယ်။

mingalar → မင်္ဂလာ

ဒီလို ရိုးရှင်းတဲ့ စိတ်ကူးတစ်ခုကနေ Myanglish IME ကို စတင်ခဲ့ပါတယ်။

အစပြုခြင်း

အစပိုင်းမှာ Myanglish ဟာ English အက္ခရာတွေကို မြန်မာစာအဖြစ် ပြောင်းပေးတဲ့ ရိုးရှင်းတဲ့ Converter တစ်ခုသာ ဖြစ်ခဲ့ပါတယ်။ ဒါပေမယ့် Chrome, LINE, Notepad, Microsoft Word နဲ့ တခြား Windows Application တွေမှာ ပုံမှန် Keyboard တစ်ခုလို အသုံးပြုနိုင်တဲ့ IME တစ်ခု ဖြစ်စေချင်ခဲ့ပါတယ်။

ဒီရည်မှန်းချက်ကြောင့် C++, Windows Text Services Framework (TSF), Unicode, CMake, Visual Studio နဲ့ PowerShell စတဲ့ နည်းပညာတွေကို လေ့လာပြီး လက်တွေ့အသုံးချခဲ့ပါတယ်။

ကိုယ်တိုင် ဒီဇိုင်းလုပ်ခဲ့တဲ့ စာရိုက်စနစ်

စာရိုက်နေချိန်မှာ English အက္ခရာတွေကို အရင်မြင်ရမယ်။ Space တစ်ချက်နှိပ်မှ မြန်မာစာအဖြစ် ပြောင်းမယ်။ နောက်ထပ် Space နှိပ်ရင် တခြားဖြစ်နိုင်တဲ့ စကားလုံးတွေကို ရွေးချယ်နိုင်မယ်။ Enter နှိပ်ရင် စကားလုံးကို အတည်ပြုမယ်။ နောက်စာလုံး စရိုက်လိုက်ရင် အရင်ရွေးထားတဲ့ စကားလုံးကို အလိုအလျောက် အတည်ပြုမယ်။

မှားသွားရင် Backspace နဲ့ မူရင်း Myanglish ကို ပြန်ယူနိုင်ဖို့၊ Esc နဲ့ Conversion ကို ပယ်ဖျက်နိုင်ဖို့လည်း စဉ်းစားခဲ့ပါတယ်။ ဒီလို အသေးစိတ်အချက်တွေကို တစ်ခုချင်း သတ်မှတ်ရင်း Keyboard တစ်ခုရဲ့ User Experience ဟာ ဘယ်လောက်အရေးကြီးလဲဆိုတာ နားလည်လာခဲ့ပါတယ်။

အခက်အခဲတွေ စတင်လာတဲ့အချိန်

Chrome မှာ အလုပ်လုပ်ပေမယ့် LINE မှာ စာရိုက်ရင်း ရပ်သွားတာ၊ Notepad မှာ Cursor နေရာမှားတာ၊ Application ပြောင်းပြီးနောက် Candidate Window ကျန်နေခဲ့တာတွေ ဖြစ်ခဲ့ပါတယ်။ Screen ပေါ်မှာ မြန်မာစာ မှန်မှန်ပြနေပေမယ့် Enter နှိပ်ပြီး အတည်ပြုလိုက်တဲ့အခါ စာလုံးပျောက်သွားတာ၊ ထပ်သွားတာမျိုးတွေလည်း ရှိခဲ့ပါတယ်။

Code တစ်နေရာကို ပြင်လိုက်ရင် တခြားနေရာမှာ ပြဿနာအသစ် ထပ်ပေါ်လာတတ်ပါတယ်။ တစ်ခါတလေ Version အသစ်က အရင် Version ထက်တောင် ပိုဆိုးသွားခဲ့ပါတယ်။ ဒါပေမယ့် Project ကို အစကနေ ပြန်မစခဲ့ပါဘူး။ Error Log တွေကို ဖတ်တယ်။ ပြဿနာဖြစ်တဲ့အကြောင်းရင်းကို ရှာတယ်။ Test Case အသစ်တွေ ဖန်တီးတယ်။ မအောင်မြင်ရင် အရင်အလုပ်လုပ်ခဲ့တဲ့ Version ကို ပြန်ယူပြီး နောက်တစ်နည်းနဲ့ စမ်းသပ်ခဲ့ပါတယ်။

မြန်မာစာရဲ့ ရှုပ်ထွေးမှုကို နားလည်လာခြင်း

Myanglish ကို ဖန်တီးရင်း အခက်ခဲဆုံးအပိုင်းတွေထဲက တစ်ခုက မြန်မာ Unicode နဲ့ ဆင့်စာလုံးတွေ ဖြစ်ပါတယ်။

မင်္ဂလာ၊ လိမ္မာ၊ လိမ္မော်၊ အိန္ဒြာ၊ အလင်္ကာ

ဒီလို စကားလုံးတွေကို မှန်ကန်စွာ ရိုက်နိုင်ဖို့ အကြိမ်ကြိမ် စမ်းသပ်ခဲ့ရပါတယ်။ မြန်မာစာမှာ Screen ပေါ်က မြင်ရတဲ့ စာလုံးပုံစံနဲ့ Computer ထဲမှာ သိမ်းဆည်းထားတဲ့ Unicode အစီအစဉ်ဟာ မတူညီနိုင်ပါဘူး။ ဒါကြောင့် စာလုံးတစ်လုံး မှန်မှန်ပေါ်လာရုံနဲ့ မလုံလောက်ဘဲ အတည်ပြုပြီးနောက် Unicode စာသားအဖြစ် မှန်ကန်နေဖို့လည်း လိုအပ်ပါတယ်။

အမှားတွေကနေ သင်ယူခဲ့ခြင်း

Development လုပ်နေစဉ်မှာ Build Error, Registration Error, Encoding Error နဲ့ Windows Security ဆိုင်ရာ အခက်အခဲတွေကိုလည်း ကြုံတွေ့ခဲ့ရပါတယ်။ အဲဒီပြဿနာတွေကို ဖြေရှင်းရင်း Software Development ဆိုတာ Code ရေးတတ်ရုံနဲ့ မပြီးဘူးဆိုတာ သိလာခဲ့ပါတယ်။

ပြဿနာကို ခွဲခြမ်းစိတ်ဖြာတတ်ဖို့၊ မှားနေတဲ့အကြောင်းရင်းကို ရှာတတ်ဖို့၊ စမ်းသပ်မှုတွေကို စနစ်တကျ လုပ်တတ်ဖို့နဲ့ အရင် Version တွေကို လုံခြုံစွာ သိမ်းဆည်းထားတတ်ဖို့လည်း လိုအပ်ပါတယ်။

Bug တစ်ခုကို ပြင်နိုင်တာထက် ဘာကြောင့် အဲဒီ Bug ဖြစ်ခဲ့လဲဆိုတာ နားလည်နိုင်ခြင်းက ပိုအရေးကြီးတယ်ဆိုတာပါ။

စိတ်ကူးတစ်ခုမှ Product တစ်ခုဆီသို့

အစပိုင်းမှာ Myanglish ဟာ ကျွန်တော့် Computer ပေါ်မှာပဲ စမ်းသပ်အသုံးပြုတဲ့ Project တစ်ခု ဖြစ်ခဲ့ပါတယ်။ နောက်ပိုင်းမှာတော့ တခြားသူတွေလည်း Download လုပ်ပြီး အသုံးပြုနိုင်တဲ့ Software တစ်ခု ဖြစ်လာစေဖို့ စတင်ပြင်ဆင်ခဲ့ပါတယ်။

Installer, Settings, User Dictionary, Version Management နဲ့ Microsoft Store Distribution အထိ ဆက်လက်လေ့လာခဲ့ပါတယ်။ 2026 ခုနှစ် စက်တင်ဘာလမှာ Myanglish v1.0.0 အတွက် Windows Package ကို ပြင်ဆင်ပြီး Microsoft Store Certification သို့ တင်သွင်းနိုင်ခဲ့ပါတယ်။ ဒါဟာ ကျွန်တော့်အတွက် အရေးကြီးတဲ့ မှတ်တိုင်တစ်ခု ဖြစ်ခဲ့ပါတယ်။ ဒါပေမယ့် Myanglish ရဲ့ ခရီးလမ်းက အဲဒီမှာ မဆုံးသေးပါဘူး။

Myanglish ရဲ့ အနာဂတ်

Myanglish ကို ရိုးရိုး စာသားပြောင်းပေးတဲ့ Keyboard တစ်ခုအဖြစ်ပဲ ရပ်တန့်မထားချင်ပါဘူး။ အသုံးပြုသူရဲ့ စာရိုက်ပုံကို ပိုမိုနားလည်နိုင်တဲ့၊ စကားလုံးတွေကို ပိုမိုတိကျစွာ အကြံပြုနိုင်တဲ့၊ စာလုံးပေါင်းအမှားတွေကို ပြင်ဆင်ဖို့ အကြံပြုပေးနိုင်တဲ့ ပိုမိုအသိဉာဏ်ရှိသော မြန်မာစာရိုက်စနစ်တစ်ခု ဖြစ်လာစေချင်ပါတယ်။

ကျွန်တော့်အတွက် Myanglish ရဲ့ အဓိပ္ပာယ်

Myanglish ဟာ ကျွန်တော့်အတွက် Programming Project တစ်ခုတည်း မဟုတ်ပါဘူး။ ကိုယ်တိုင် စိတ်ကူးထုတ်ခဲ့တဲ့ အရာတစ်ခုကို လက်တွေ့ဖြစ်လာအောင် ကြိုးစားခဲ့တဲ့ မှတ်တမ်းတစ်ခု ဖြစ်ပါတယ်။

မသိတဲ့ နည်းပညာတွေကို လေ့လာခဲ့ရတယ်။ မအောင်မြင်တဲ့ Version တွေကို ပြန်ပြင်ခဲ့ရတယ်။ စာလုံးတစ်လုံး မှန်ကန်ဖို့အတွက် နာရီပေါင်းများစွာ စမ်းသပ်ခဲ့ရတဲ့ အချိန်တွေလည်း ရှိခဲ့ပါတယ်။

ဒီ Project ကနေ ကျွန်တော်ရရှိခဲ့တဲ့ အကြီးမားဆုံးအရာဟာ Software တစ်ခုတည်း မဟုတ်ပါဘူး။ စိတ်ကူးတစ်ခုကို အကောင်အထည်ဖော်နိုင်တဲ့ ယုံကြည်မှု၊ ပြဿနာတွေကို ကိုယ်တိုင်ဖြေရှင်းနိုင်တဲ့ အတွေ့အကြုံနဲ့ မအောင်မြင်တဲ့အခါ လက်မလျှော့ဘဲ ဆက်လက်ကြိုးစားနိုင်တဲ့ စိတ်ဓာတ်တို့ ဖြစ်ပါတယ်။

Myanglish ကို စတင်ခဲ့တဲ့နေ့ကနေ ဒီနေ့အထိ ကျွန်တော့်ရဲ့ ရည်မှန်းချက်က မပြောင်းလဲခဲ့ပါဘူး။

မြန်မာစာကို လူတိုင်း ပိုမိုလွယ်ကူပြီး သဘာဝကျကျ ရိုက်နိုင်စေချင်ပါတယ်။

Myanglish ဟာ ပြီးဆုံးသွားတဲ့ Project တစ်ခုမဟုတ်ဘဲ ဆက်လက်တိုးတက်နေမယ့် ခရီးလမ်းတစ်ခု ဖြစ်ပါတယ်။

Myanglish — by MAUNG KAUNG

စိတ်ကူးတစ်ခုမှ စတင်ခဲ့ပြီး အမှားများမှ သင်ယူကာ အခက်အခဲများကို ကျော်ဖြတ်ရင်း တစ်ဆင့်ချင်း ဖန်တီးလာခဲ့သော မြန်မာစာရိုက်စနစ်။)MYSTORY";

LRESULT CALLBACK ReaderProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SIZE) {
        if (HWND edit = GetDlgItem(window, kReaderEditId)) {
            MoveWindow(edit, 12, 12, LOWORD(lParam) > 24 ? LOWORD(lParam) - 24 : 1,
                       HIWORD(lParam) > 24 ? HIWORD(lParam) - 24 : 1, TRUE);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openReader(HWND owner, bool isStory) {
    static bool registered = false;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ReaderProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"MyanglishInfoReader";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        registered = true;
    }
    HWND reader = CreateWindowExW(0, L"MyanglishInfoReader",
        isStory ? L"Myanglish - My Story" : L"Myanglish - About",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 750, 610,
        owner, nullptr, instance, nullptr);
    if (!reader) return;
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        isStory ? kStoryText : kAboutText,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        12, 12, 710, 550, reader,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReaderEditId)), instance, nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ShowWindow(reader, SW_SHOW);
    UpdateWindow(reader);
}

std::filesystem::path settingsPath() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (size > 0 && size < std::size(buffer)) {
        return std::filesystem::path(buffer) / "MyanglishIME" / "settings.ini";
    }
    return std::filesystem::current_path() / "settings.ini";
}

std::filesystem::path userDictionaryPath() {
    wchar_t buffer[32768]{};
    const DWORD size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer))
    );
    if (size > 0 && size < std::size(buffer)) {
        return std::filesystem::path(buffer) / "MyanglishIME" / "user_dictionary.csv";
    }
    return std::filesystem::current_path() / "user_dictionary.csv";
}

std::string utf8FromWide(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), needed, nullptr, nullptr
    );
    return result;
}

std::wstring getText(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    if (control == nullptr) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    return result;
}

bool loadLiveCandidates() {
    std::ifstream file(settingsPath(), std::ios::binary);
    if (!file.is_open()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "live_candidates=1" || line == "live_candidates=true" || line == "live_candidates=on") {
            return true;
        }
        if (line == "live_candidates=0" || line == "live_candidates=false" || line == "live_candidates=off") {
            return false;
        }
    }
    return true;
}

bool saveLiveCandidates(bool enabled) {
    try {
        const auto path = settingsPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << "# Myanglish IME settings\n";
        file << "live_candidates=" << (enabled ? "1" : "0") << "\n";
        return file.good();
    } catch (...) {
        return false;
    }
}

bool appendPersonalWord(const std::wstring& myanglishWide, const std::wstring& myanmarWide) {
    if (myanglishWide.empty() || myanmarWide.empty()) {
        return false;
    }

    const std::string myanglish = utf8FromWide(myanglishWide);
    const std::string myanmar = utf8FromWide(myanmarWide);
    if (myanglish.empty() || myanmar.empty()) {
        return false;
    }

    // Keep the personal CSV deliberately simple and safe.
    if (myanglish.find(',') != std::string::npos
        || myanmar.find(',') != std::string::npos
        || myanglish.find('\n') != std::string::npos
        || myanmar.find('\n') != std::string::npos) {
        return false;
    }

    try {
        const auto path = userDictionaryPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        const bool needsHeader = !std::filesystem::exists(path, ec)
            || std::filesystem::file_size(path, ec) == 0;

        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        if (needsHeader) {
            file << "myanglish,burmese,frequency\n";
        }

        // Personal words intentionally outrank shipped defaults.
        file << myanglish << "," << myanmar << ",2000000\n";
        return file.good();
    } catch (...) {
        return false;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateWindowW(
            L"STATIC", L"Candidate behavior",
            WS_CHILD | WS_VISIBLE,
            24, 18, 240, 22,
            window, nullptr, nullptr, nullptr
        );

        HWND checkbox = CreateWindowW(
            L"BUTTON", L"Show candidate popup on first Space",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            24, 45, 300, 26,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCheckboxId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"ON (default): First Space opens all candidates with #1 selected. OFF: legacy second-Space popup.",
            WS_CHILD | WS_VISIBLE,
            44, 76, 500, 22,
            window, nullptr, nullptr, nullptr
        );

        SendMessageW(
            checkbox, BM_SETCHECK,
            loadLiveCandidates() ? BST_CHECKED : BST_UNCHECKED, 0
        );

        CreateWindowW(
            L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            450, 42, 100, 32,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Add a missing word (Level 3)",
            WS_CHILD | WS_VISIBLE,
            24, 120, 300, 24,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Myanglish:",
            WS_CHILD | WS_VISIBLE,
            24, 154, 100, 22,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            125, 150, 180, 28,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMyanglishEditId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC", L"Myanmar:",
            WS_CHILD | WS_VISIBLE,
            24, 190, 100, 22,
            window, nullptr, nullptr, nullptr
        );

        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            125, 186, 180, 28,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMyanmarEditId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"BUTTON", L"Add personal word",
            WS_CHILD | WS_VISIBLE,
            330, 168, 220, 38,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddWordButtonId)),
            nullptr, nullptr
        );

        CreateWindowW(
            L"STATIC",
            L"Personal words are stored in LocalAppData and survive updates. Switch Myanglish off/on after adding.",
            WS_CHILD | WS_VISIBLE,
            24, 232, 540, 40,
            window, nullptr, nullptr, nullptr
        );
        CreateWindowW(L"STATIC", L"Information", WS_CHILD | WS_VISIBLE,
            24, 278, 160, 22, window, nullptr, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"About", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            175, 276, 170, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutButtonId)), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"My Story", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 276, 190, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStoryButtonId)), nullptr, nullptr);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == kAboutButtonId || LOWORD(wParam) == kStoryButtonId) {
            openReader(window, LOWORD(wParam) == kStoryButtonId);
            return 0;
        }

        if (LOWORD(wParam) == kSaveButtonId) {
            HWND checkbox = GetDlgItem(window, kCheckboxId);
            const bool enabled = SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const bool saved = saveLiveCandidates(enabled);
            MessageBoxW(
                window,
                saved ? L"Settings saved." : L"Could not save settings.ini.",
                L"Myanglish IME Settings",
                MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONERROR)
            );
            return 0;
        }

        if (LOWORD(wParam) == kAddWordButtonId) {
            const std::wstring raw = getText(window, kMyanglishEditId);
            const std::wstring myanmar = getText(window, kMyanmarEditId);

            if (appendPersonalWord(raw, myanmar)) {
                SetWindowTextW(GetDlgItem(window, kMyanglishEditId), L"");
                SetWindowTextW(GetDlgItem(window, kMyanmarEditId), L"");
                MessageBoxW(
                    window,
                    L"Personal word added.\n\nSwitch away from Myanglish and back to reload it.",
                    L"Myanglish IME Settings",
                    MB_OK | MB_ICONINFORMATION
                );
            } else {
                MessageBoxW(
                    window,
                    L"Enter both Myanglish and Myanmar text. Commas/newlines are not allowed.",
                    L"Myanglish IME Settings",
                    MB_OK | MB_ICONERROR
                );
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    constexpr wchar_t kClassName[] = L"MyanglishSettingsWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kClassName,
        L"Myanglish IME Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 380,
        nullptr, nullptr, instance, nullptr
    );

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
