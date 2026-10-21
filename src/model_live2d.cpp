#include "model_live2d.h"

#include <windows.h>
#include <wincodec.h>

#include <wrl/client.h>

#include <CubismFramework.hpp>
#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <Model/CubismMoc.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismPose.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Id/CubismIdManager.hpp>
#include <Rendering/D3D11/CubismRenderer_D3D11.hpp>
#include <Utils/CubismString.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <malloc.h>
#include <vector>

#include "behavior.h"
#include "com_ptr.h"
#include "utf8_file.h"

// Live2D 模型加载器:模型、贴图、运动、物理、呼吸、眨眼,并做 D3D11 渲染。
// 参考 SDK Samples/D3D11/Demo 的 LAppModel / LAppPal / LAppTextureManager,但剥离
// 了示例工程的单例与 UI,只保留桌面宠物需要的最小闭环。

namespace L2D = Live2D::Cubism::Framework;

// ---------------- 基础工具 ----------------

// 日志写到 exe 目录(ExeDir 定义见下,这里前置声明)
static const std::string& ExeDir();

static void LogLine(const char* what, const char* msg) {
  FILE* f = utf8::Fopen(ExeDir() + "live2d_error.log", "a");
  if (f) {
    fprintf(f, "%s: %s\n", what, msg);
    fclose(f);
  }
}

void LogLive2dError(const char* what, const char* msg) { LogLine(what, msg); }

// UTF-8 路径 → 宽字符(Cubism 的路径都是 UTF-8,Windows API 需要宽字符)
static std::wstring ToWide(const std::string& s) {
  if (s.empty()) return L"";
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

static std::string ToUtf8(const std::wstring& w) {
  if (w.empty()) return "";
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

// exe 所在目录(以 '\\' 结尾)。资源(FrameworkShaders/assets)都复制到 exe 旁边,
// 相对路径一律对 exe 目录解析,与启动时的工作目录无关。
static const std::string& ExeDir() {
  static const std::string dir = []() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf, n);
    const size_t pos = w.find_last_of(L"\\/");
    return ToUtf8(pos == std::wstring::npos ? std::wstring() : w.substr(0, pos + 1));
  }();
  return dir;
}

static std::string ResolvePath(const std::string& p) {
  if (p.empty()) return p;
  // 绝对路径(盘符或前导斜杠)原样返回;相对路径拼到 exe 目录下
  if (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':')) return p;
  return ExeDir() + p;
}

static std::string ReadFileBytes(const std::string& path, size_t* size) {
  FILE* f = utf8::Fopen(ResolvePath(path), "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0) {
    fclose(f);
    return {};
  }
  std::string buf(static_cast<size_t>(n), '\0');
  const size_t got = fread(&buf[0], 1, static_cast<size_t>(n), f);
  fclose(f);
  if (got != static_cast<size_t>(n)) return {};
  if (size) *size = static_cast<size_t>(n);
  return buf;
}

// 在模型目录下找到 *.model3.json 的纯文件名(目录需以 '/' 或 '\\' 结尾)。
// 不同模型目录里文件名不同(Haru.model3.json / Hiyori.model3.json …),不写死。
static std::string FindModelJson(const std::string& dir) {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(ToWide(dir + "*.model3.json").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return {};
  const std::string name = ToUtf8(fd.cFileName);
  FindClose(h);
  return name;
}

// ---------------- Cubism Framework 全局回调 ----------------
// Framework 自己读 fx 着色器源码、我们读模型 json/moc3/motion3 都走这两个函数。

static L2D::csmByte* CubismLoadFile(const std::string path, L2D::csmSizeInt* outSize) {
  size_t size = 0;
  std::string data = ReadFileBytes(path, &size);
  if (data.empty() || size == 0) {
    if (outSize) *outSize = 0;
    return nullptr;
  }
  auto* buf = static_cast<L2D::csmByte*>(CSM_MALLOC(size));
  memcpy(buf, data.data(), size);
  if (outSize) *outSize = static_cast<L2D::csmSizeInt>(size);
  return buf;
}

static void CubismReleaseBytes(L2D::csmByte* data) { CSM_FREE(data); }

static void CubismLog(const char* msg) {
  FILE* f = utf8::Fopen(ExeDir() + "pet_live2d.log", "a");
  if (f) {
    fprintf(f, "%s\n", msg);
    fclose(f);
  }
}

// CubismFramework 需要非空分配器;SDK 内部用 CSM_MALLOC/CSM_NEW 时会要求。
class MallocAllocator final : public L2D::ICubismAllocator {
 public:
  void* Allocate(const L2D::csmSizeType size) override { return ::operator new(size); }
  void Deallocate(void* memory) override { ::operator delete(memory); }
  void* AllocateAligned(const L2D::csmSizeType size, const L2D::csmUint32 alignment) override {
    return _aligned_malloc(size, alignment);
  }
  void DeallocateAligned(void* alignedMemory) override { _aligned_free(alignedMemory); }
};

// CubismFramework 全局初始化(进程内只做一次)
static void EnsureCubismStarted() {
  static bool started = false;
  if (started) return;
  static MallocAllocator allocator;
  // 注意:Option 必须是 static —— StartUp 只保存传入的指针(s_option = option),
  // 函数局部变量会在返回后悬空,后续 shader 编译读 LoadFileFunction 就是野指针。
  static L2D::CubismFramework::Option opt{};
  opt.LogFunction = CubismLog;
  opt.LoggingLevel = L2D::CubismFramework::Option::LogLevel_Warning;
  opt.LoadFileFunction = CubismLoadFile;
  opt.ReleaseBytesFunction = CubismReleaseBytes;
  if (!L2D::CubismFramework::StartUp(&allocator, &opt)) {
    throw ModelLoadError{"CubismFramework::StartUp failed"};
  }
  L2D::CubismFramework::Initialize();
  started = true;
}

// ---------------- 实现 ----------------

struct Live2DModel::Impl : L2D::CubismUserModel {
  // --- 加载相关 ---
  L2D::ICubismModelSetting* setting = nullptr;
  std::string home_dir;

  // 画布尺寸(从 Model::meta_ 拷入,Impl 不是 Model 子类,不能直接访问)
  int canvas_w = 400;
  int canvas_h = 400;

  // --- 相机 ---
  // MVP = 裁剪投影 M1 (SDK 坐标 → NDC) × 模型布局 M2 (canvas → SDK 单位)
  // × 贴边缩放 M3。Draw 每帧重算,便于后续窗口改尺寸/换边缩放。
  L2D::CubismMatrix44 mvp_;

  // --- 行为参数映射 ---
  Behavior* b = nullptr;
  L2D::CubismIdHandle id_angle_x = nullptr;
  L2D::CubismIdHandle id_angle_y = nullptr;
  L2D::CubismIdHandle id_angle_z = nullptr;
  L2D::CubismIdHandle id_body_angle_z = nullptr;
  L2D::CubismIdHandle id_mouth_open_y = nullptr;
  L2D::CubismIdHandle id_param_breath = nullptr;
  L2D::CubismIdHandle id_eye_ball_x = nullptr;
  L2D::CubismIdHandle id_eye_ball_y = nullptr;

  // --- 运动(内存持有,播放用) ---
  std::vector<std::pair<std::string, L2D::ACubismMotion*>> motions;  // "idle_0" 等
  float idle_restart_timer = 0.f;

  // --- 渲染设备 ---
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  // BindTexture 只保存裸指针(Framework 约定),贴图必须由本类持有到销毁
  std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures_;

  // --- 呼吸 ---
  L2D::CubismBreath* breath = nullptr;
  float breath_t = 0.f;

  Impl() : CubismUserModel() {}
  ~Impl() override {
    for (auto& kv : motions) L2D::ACubismMotion::Delete(kv.second);
    motions.clear();
    L2D::CubismBreath::Delete(breath);
    if (setting) {
      delete setting;
      setting = nullptr;
    }
  }

  // --- 加载 ---

  void Load(const std::string& dir) {
    EnsureCubismStarted();

    // home_dir 统一解析到绝对路径:WIC 打开贴图走 Windows 路径,不能依赖 CWD
    home_dir = ResolvePath(dir);
    while (!home_dir.empty() && (home_dir.back() == '/' || home_dir.back() == '\\')) home_dir.pop_back();
    home_dir += '/';

    // model3.json → ICubismModelSetting(文件名不写死,目录内唯一 *.model3.json)
    const std::string model_json = FindModelJson(home_dir);
    if (model_json.empty()) throw ModelLoadError{"no *.model3.json in model dir"};
    size_t jsonSize = 0;
    std::string json = ReadFileBytes(home_dir + model_json, &jsonSize);
    if (json.empty()) throw ModelLoadError{"model3.json not found or empty"};

    setting = new L2D::CubismModelSettingJson(
        reinterpret_cast<L2D::csmByte*>(json.data()), static_cast<L2D::csmSizeInt>(jsonSize));
    SetupModel(setting);

    if (!GetModel()) throw ModelLoadError{"CubismMoc::CreateModel failed (moc3 missing or corrupt)"};

    // 渲染器(必须带设备;D3D11 后端需要恒定设置)
    if (device) {
      CreateRenderer(static_cast<L2D::csmUint32>(canvas_w),
                     static_cast<L2D::csmUint32>(canvas_h));
      L2D::Rendering::CubismRenderer_D3D11* r =
          GetRenderer<L2D::Rendering::CubismRenderer_D3D11>();
      if (!r) throw ModelLoadError{"CubismRenderer_D3D11 create failed"};
      r->IsPremultipliedAlpha(true);  // 与窗口预乘交换链一致
      SetupTextures();
      // 注意:不要开 scissor。Framework 只开 Enable 不设 rect,而 D3D11 规范要求
      // scissor 关闭时 rect 必须能覆盖整个渲染目标——本机 NVIDIA 驱动对该组合
      // (ScissorEnable + rect=NULL)直接在驱动内崩溃。遮罩绘制交给驱动默认
      // (scissor 关,全屏 rect),先保证能跑;mask 效果可后续再细化。
    }

    // 行为参数 Id(不存在则为空,SetParameterValue 会忽略)
    L2D::CubismIdManager* ids = L2D::CubismFramework::GetIdManager();
    using namespace Live2D::Cubism::Framework::DefaultParameterId;
    id_angle_x = ids->GetId(ParamAngleX);
    id_angle_y = ids->GetId(ParamAngleY);
    id_angle_z = ids->GetId(ParamAngleZ);
    id_body_angle_z = ids->GetId(ParamBodyAngleZ);
    id_mouth_open_y = ids->GetId(ParamMouthOpenY);
    id_param_breath = ids->GetId(ParamBreath);
    id_eye_ball_x = ids->GetId(ParamEyeBallX);
    id_eye_ball_y = ids->GetId(ParamEyeBallY);
  }

  void SetupModel(L2D::ICubismModelSetting* s) {
    IsUpdating(true);

    // moc3 → CubismMoc → CubismModel
    const char* mocName = s->GetModelFileName();
    if (mocName && mocName[0]) {
      size_t size = 0;
      std::string data = ReadFileBytes(home_dir + mocName, &size);
      if (!data.empty()) {
        LoadModel(reinterpret_cast<L2D::csmByte*>(data.data()),
                  static_cast<L2D::csmSizeInt>(size), false);
      }
    }

    // 物理(头发摆动,Haru 有)
    {
      const char* p = s->GetPhysicsFileName();
      if (p && p[0]) {
        size_t size = 0;
        std::string data = ReadFileBytes(home_dir + p, &size);
        if (!data.empty()) {
          LoadPhysics(reinterpret_cast<L2D::csmByte*>(data.data()),
                      static_cast<L2D::csmSizeInt>(size));
        }
      }
    }

    // 姿态(pose,Haru 有)
    {
      const char* p = s->GetPoseFileName();
      if (p && p[0]) {
        size_t size = 0;
        std::string data = ReadFileBytes(home_dir + p, &size);
        if (!data.empty()) {
          LoadPose(reinterpret_cast<L2D::csmByte*>(data.data()),
                   static_cast<L2D::csmSizeInt>(size));
        }
      }
    }

    // 眨眼:用模型自带的参数配置
    if (s->GetEyeBlinkParameterCount() > 0) {
      _eyeBlink = L2D::CubismEyeBlink::Create(s);
    }

    // 呼吸:标准参数(与官方样例一致)
    {
      breath = L2D::CubismBreath::Create();
      L2D::csmVector<L2D::CubismBreath::BreathParameterData> bp;
      bp.PushBack(L2D::CubismBreath::BreathParameterData(
          L2D::CubismFramework::GetIdManager()->GetId(
              Live2D::Cubism::Framework::DefaultParameterId::ParamAngleX),
          0.f, 15.f, 6.5345f, 0.5f));
      bp.PushBack(L2D::CubismBreath::BreathParameterData(
          L2D::CubismFramework::GetIdManager()->GetId(
              Live2D::Cubism::Framework::DefaultParameterId::ParamAngleY),
          0.f, 8.f, 3.5345f, 0.5f));
      bp.PushBack(L2D::CubismBreath::BreathParameterData(
          L2D::CubismFramework::GetIdManager()->GetId(
              Live2D::Cubism::Framework::DefaultParameterId::ParamAngleZ),
          0.f, 10.f, 5.5345f, 0.5f));
      bp.PushBack(L2D::CubismBreath::BreathParameterData(
          L2D::CubismFramework::GetIdManager()->GetId(
              Live2D::Cubism::Framework::DefaultParameterId::ParamBodyAngleX),
          0.f, 4.f, 15.5345f, 0.5f));
      bp.PushBack(L2D::CubismBreath::BreathParameterData(
          L2D::CubismFramework::GetIdManager()->GetId(
              Live2D::Cubism::Framework::DefaultParameterId::ParamBreath),
          0.5f, 0.5f, 3.2345f, 0.5f));
      breath->SetParameters(bp);
    }

    // 运动组(Idle 组)全部加载到内存
    for (L2D::csmInt32 g = 0; g < s->GetMotionGroupCount(); ++g) {
      const char* group = s->GetMotionGroupName(g);
      if (!group || strcmp(group, "Idle") != 0) continue;  // 目前只播放 Idle 组
      const L2D::csmInt32 count = s->GetMotionCount(group);
      for (L2D::csmInt32 i = 0; i < count; ++i) {
        const char* file = s->GetMotionFileName(group, i);
        if (!file || !file[0]) continue;
        size_t size = 0;
        std::string data = ReadFileBytes(home_dir + file, &size);
        if (data.empty()) continue;
        const std::string name =
            std::string(L2D::Utils::CubismString::GetFormatedString("%s_%d", group, i).GetRawString());
        auto* m = static_cast<L2D::CubismMotion*>(
            LoadMotion(reinterpret_cast<L2D::csmByte*>(data.data()),
                       static_cast<L2D::csmSizeInt>(size), name.c_str()));
        if (m) motions.emplace_back(name, m);
      }
    }

    // 布局(模型画布 → SDK 单位)。没 layout 时用画布尺寸。
    {
      L2D::csmMap<L2D::csmString, L2D::csmFloat32> layout;
      s->GetLayoutMap(layout);
      GetModelMatrix()->SetupFromLayout(layout);
      if (GetModelMatrix()->GetScaleX() == 0.f) {
        // 极少数模型没有 layout 也没有尺寸——回退为 1:1
        L2D::CubismMatrix44* mm = GetModelMatrix();
        mm->LoadIdentity();
        mm->Scale(1.f, 1.f);
      }
    }

    GetModel()->SaveParameters();
    IsUpdating(false);
    IsInitialized(true);
  }

  void SetupTextures() {
    // WIC 解码 PNG → D3D11 贴图。Cubism 贴图未预乘,这里预乘后交给预乘管线。
    for (L2D::csmInt32 i = 0; i < setting->GetTextureCount(); ++i) {
      const char* file = setting->GetTextureFileName(i);
      if (!file || !file[0]) continue;
      LoadTexture(i, home_dir + file);
    }
  }

  void LoadTexture(L2D::csmInt32 index, const std::string& path) {
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    Microsoft::WRL::ComPtr<IWICBitmap> source;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { LogLine("texture", "WIC factory failed"); return; }

    hr = factory->CreateDecoderFromFilename(ToWide(ResolvePath(path)).c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { LogLine("texture", ("decode failed: " + path).c_str()); return; }

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return;

    // 统一转 32 位预乘 RGBA,保证 alpha 语义确定
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) return;
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA,
                          WICBitmapDitherTypeNone, nullptr, 0.f,
                          WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
      // 不支持预乘转换的罕见情况:退到普通 BGRA,加载后手动预乘
      Microsoft::WRL::ComPtr<IWICFormatConverter> conv2;
      hr = factory->CreateFormatConverter(&conv2);
      if (FAILED(hr)) return;
      hr = conv2->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.f,
                             WICBitmapPaletteTypeCustom);
      if (FAILED(hr)) return;

      UINT w = 0, h = 0;
      conv2->GetSize(&w, &h);
      std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
      hr = conv2->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data());
      if (FAILED(hr)) return;
      for (UINT j = 0; j < h; ++j) {
        auto* row = px.data() + static_cast<size_t>(j) * w * 4;
        for (UINT i = 0; i < w; ++i) {
          const float a = row[i * 4 + 3] / 255.f;
          row[i * 4 + 0] = static_cast<unsigned char>(row[i * 4 + 0] * a);
          row[i * 4 + 1] = static_cast<unsigned char>(row[i * 4 + 1] * a);
          row[i * 4 + 2] = static_cast<unsigned char>(row[i * 4 + 2] * a);
        }
      }
      // 抗锯齿:带 mip 链(DEFAULT + GENERATE_MIPS),缩小时走三线性采样
      // (Sampler_Drawable 是 MIN_MAG_MIP_LINEAR,贴图无 mip 则退化为直接下采样)。
      D3D11_TEXTURE2D_DESC td{};
      td.Width = w;
      td.Height = h;
      td.MipLevels = 0;  // 0 = 完整 mip 链
      td.ArraySize = 1;
      td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;  // GenerateMips 需要 DEFAULT(可绑 RTV)
      td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
      if (FAILED(device->CreateTexture2D(&td, nullptr, tex.GetAddressOf()))) return;
      // 只上传 mip 0,其余由 GPU 生成
      context->UpdateSubresource(tex.Get(), 0, nullptr, px.data(), w * 4, 0);
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
      if (SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) {
        context->GenerateMips(srv.Get());
        textures_.push_back(srv);  // 持有到模型销毁(BindTexture 只存裸指针)
        GetRenderer<L2D::Rendering::CubismRenderer_D3D11>()->BindTexture(index, srv.Get());
      }
      return;
    }

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    const size_t stride = static_cast<size_t>(w) * 4;
    std::vector<unsigned char> px(stride * h);
    hr = conv->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(px.size()), px.data());
    if (FAILED(hr)) return;

    // WIC 32bppPRGBA = R,G,B,A 各 8 位、已预乘。与 R8G8B8A8_UNORM 字节序一致。
    // 带 mip 链(DEFAULT + GENERATE_MIPS):缩小时走三线性采样抗锯齿(贴图无 mip
    // 则直接下采样,缩小后边缘会闪)。mip 由 GPU 生成,逐级下采样含 alpha 抖动。
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 0;  // 0 = 完整 mip 链
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;  // GenerateMips 需要 DEFAULT(可绑 RTV)
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, nullptr, tex.GetAddressOf()))) return;
    // 只上传 mip 0,其余由 GPU 生成
    context->UpdateSubresource(tex.Get(), 0, nullptr, px.data(), static_cast<UINT>(stride), 0);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) {
      context->GenerateMips(srv.Get());
      textures_.push_back(srv);  // 持有到模型销毁(BindTexture 只存裸指针)
      GetRenderer<L2D::Rendering::CubismRenderer_D3D11>()->BindTexture(index, srv.Get());
    }
  }

  // --- 每帧 ---

  void Update(float dt) {
    if (!GetModel()) return;

    // 模型参数加载(上一帧保存的状态)
    GetModel()->LoadParameters();

    // 运动:空闲随机循环。点击的 Happy 动画直接走行为参数驱动,不占用运动。
    // 「随机动画」关闭时:停止一切动作,并把全部参数重置回模型默认值(中性姿势),
    // 让宠物原地完全静止——后续只剩眨眼/呼吸/鼠标追随继续叠加。
    // b==null(无行为层)走原逻辑,不受影响。
    if (b && !b->random_animation) {
      _motionManager->StopAllMotions();
      for (L2D::csmInt32 i = 0; i < GetModel()->GetParameterCount(); ++i) {
        GetModel()->SetParameterValue(i, GetModel()->GetParameterDefaultValue(i), 1.f);
      }
    } else if (_motionManager->IsFinished()) {
      idle_restart_timer -= dt;
      if (idle_restart_timer <= 0.f) {
        StartRandomIdle();
        idle_restart_timer = 4.f;  // 与 SDK 样例一致:间隔 4 秒
      }
    } else {
      _motionManager->UpdateMotion(GetModel(), dt);
    }

    // 物理(头发)+ 姿态
    if (_physics) _physics->Evaluate(GetModel(), dt);
    if (_pose) _pose->UpdateParameters(GetModel(), dt);

    // 眨眼
    if (_eyeBlink) _eyeBlink->UpdateParameters(GetModel(), dt);

    // 呼吸
    if (breath) {
      breath_t += dt;
      breath->UpdateParameters(GetModel(), dt);
    }

    // 行为参数映射:朝左翻转 ParamAngleZ / ParamBodyAngleZ;点击兴奋张嘴。
    if (b) {
      const float side = b->facing_left ? -1.f : 1.f;
      const float move = b->is_moving ? 1.f : 0.f;
      if (id_angle_z) GetModel()->AddParameterValue(id_angle_z, move * side * 5.f, 0.4f);
      if (id_body_angle_z) GetModel()->AddParameterValue(id_body_angle_z, side * 4.f, 0.3f);
      if (id_mouth_open_y) GetModel()->AddParameterValue(id_mouth_open_y, b->happiness * 0.9f, 1.f);
    }

    // 鼠标追随:眼球看向光标 + 头部随光标转动(look_x/y 由 main 每帧写入,已归一化到 ±1)。
    // 头部用 SetParameterValue 覆盖 Idle motion 的随机头转——开启跟随时"看光标"应占主导;
    // 缺 ParamEyeBallX/Y / ParamAngleX/Y 的模型 GetId 返回 null,空指针保护下自动退化。
    if (b && b->follow_mouse) {
      if (id_eye_ball_x) GetModel()->AddParameterValue(id_eye_ball_x, b->look_x, 0.5f);
      if (id_eye_ball_y) GetModel()->AddParameterValue(id_eye_ball_y, b->look_y, 0.5f);
      // 头随光标:左右看映射到 ParamAngleY,俯仰映射到 ParamAngleX。方向与官方样例一致。
      if (id_angle_x) GetModel()->SetParameterValue(id_angle_x, b->look_y * 20.f, 1.0f);
      if (id_angle_y) GetModel()->SetParameterValue(id_angle_y, b->look_x * 20.f, 1.0f);
    }

    // 模型最终更新
    GetModel()->Update();
    GetModel()->SaveParameters();
  }

  void StartRandomIdle() {
    if (motions.empty()) return;
    // 用简单均匀随机(仅选运动)
    const size_t i = static_cast<size_t>(rand()) % motions.size();
    _motionManager->StartMotionPriority(motions[i].second, false, 2);  // 2 = Normal
  }

  void Draw() {
    auto* r = GetRenderer<L2D::Rendering::CubismRenderer_D3D11>();
    if (!r || !context) return;

    // 1) MVP:与官方 D3D11 样例一致——模型经 SetupFromLayout 后已在 ±1 单位空间,
    // 投影只需按窗口宽高比缩放(方形窗口即单位矩阵),再乘模型矩阵。
    // 注意不是 2/canvas_h:canvas 尺寸已由 modelMatrix 归一化掉,再乘 2/h 会把
    // 模型缩到几个像素,屏幕上什么都看不到。
    mvp_.LoadIdentity();
    const float w = static_cast<float>(canvas_w);
    const float h = static_cast<float>(canvas_h);
    if (GetModel()->GetCanvasWidth() > 1.0f && w < h) {
      // 横长模型 + 竖窗口:按模型宽度适配(SetWidth 幂等,官方每帧调用)
      GetModelMatrix()->SetWidth(2.0f);
      mvp_.Scale(1.0f, w / h);
    } else {
      mvp_.Scale(h / w, 1.0f);
    }
    mvp_.MultiplyByMatrix(GetModelMatrix());
    // 贴边缩放(目前未用,预留)
    r->SetMvpMatrix(&mvp_);

    // 2) 帧头:SDK 内部状态机(掩码缓冲等)
    r->StartFrame(context);

    // 3) 绘制(不开 scissor——见 Load 里注释)
    r->DrawModel();

    // 4) 帧尾
    r->EndFrame();
  }
};

// ---------------- Model 接口 ----------------

Live2DModel::~Live2DModel() { delete impl_; }

void Live2DModel::SetDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
  device_ = dev;
  context_ = ctx;
  if (!impl_) {
    // 必须先 StartUp:CubismUserModel 的基类构造函数用 CSM_NEW 走
    // CubismFramework 的 placement new,而它依赖 StartUp 传入的分配器。
    // 若先构造 Impl 再 StartUp,s_allocator 还是 NULL,构造即崩溃。
    EnsureCubismStarted();

    // 着色器字节码磁盘缓存目录(exe 旁 ShaderCache/)。首次启动在此缓存编译结果,
    // 之后跳过 ~500 次 D3DCompile 秒开。static 保证生命周期覆盖整个进程
    // (Framework 的 SetCacheDirectory 只存指针不拷贝)。
    static const std::string cacheDir = ExeDir() + "ShaderCache/";
    L2D::Rendering::CubismShader_D3D11::SetCacheDirectory(cacheDir.c_str());

    impl_ = new Impl();
  }
  impl_->device = dev;
  impl_->context = ctx;
  impl_->canvas_w = meta_.canvas_w;
  impl_->canvas_h = meta_.canvas_h;
  // 模型加载前必须设置(官方样例同样要求)
  if (dev) {
    L2D::Rendering::CubismRenderer_D3D11::SetConstantSettings(1, dev);
  }
}

void Live2DModel::Load(const std::string& dir) {
  // 切换模型:销毁旧 Impl(CubismUserModel 析构会释放 moc/模型/渲染器/贴图/运动/
  // 物理/姿态/眨眼等全部资源),再重建全新实例。SetDevice 已 StartUp 并设置好
  // 着色器缓存与常量缓冲,这里复用成员里的设备/上下文与当前画布尺寸。
  EnsureCubismStarted();
  if (impl_) {
    delete impl_;
    impl_ = nullptr;
  }
  impl_ = new Impl();
  impl_->device = device_;
  impl_->context = context_;
  impl_->canvas_w = meta_.canvas_w;
  impl_->canvas_h = meta_.canvas_h;
  // 重建渲染器前重申常量缓冲设置(static 全局,幂等;首次已在 SetDevice 里设过)
  if (device_) {
    L2D::Rendering::CubismRenderer_D3D11::SetConstantSettings(1, device_);
  }
  impl_->Load(dir);
}

void Live2DModel::Update(float dt) {
  if (impl_) {
    impl_->b = b_;
    impl_->Update(dt);
  }
}

void Live2DModel::Draw() {
  if (impl_) impl_->Draw();
}

void Live2DModel::Resize(int width, int height) {
  meta_.canvas_w = width;
  meta_.canvas_h = height;
  if (!impl_) return;
  impl_->canvas_w = width;
  impl_->canvas_h = height;
  // 更新渲染器视口尺寸:PreDraw() 每帧据此重设 viewport,矢量网格按新尺寸
  // 高分辨率重绘(见 CubismRenderer_D3D11.cpp::PreDraw)。非 blend 模式下
  // _modelRenderTargets 为空,无需重建;mask 缓冲固定 256,不受影响。
  impl_->SetRenderTargetSize(static_cast<L2D::csmUint32>(width),
                             static_cast<L2D::csmUint32>(height));
}

// ---------------- 工厂 ----------------

std::unique_ptr<Model> CreateModel() { return std::make_unique<Live2DModel>(); }

bool Live2DShaderCacheNeedsCompile() {
  return !L2D::Rendering::CubismShader_D3D11::IsCacheReady();
}
