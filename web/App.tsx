import React, { useState, useEffect } from 'react';
import { 
  Cpu, 
  Activity, 
  Flame, 
  Zap, 
  Server, 
  Radio, 
  Monitor, 
  ShieldCheck, 
  Terminal, 
  Play, 
  RotateCw,
  Sliders,
  CheckCircle2,
  HardDrive
} from 'lucide-react';

interface Telemetry {
  cpuTemp: number;
  batteryTemp: number;
  ramFreeGb: number;
  zramFreeGb: number;
  primeFreqMhz: number;
  peltierLocked: boolean;
  activeCore: string;
  npuTops: number;
  npuStatus: string;
  gpuGflops: number;
  gpuStatus: string;
  inferencesCount: number;
  lastThroughputTps: number;
  rearDisplayMsg: string;
  networkIface: string;
  ipAddress: string;
  powerGovernor: 'powersave' | 'performance' | 'schedutil';
}

export default function App() {
  const [telemetry, setTelemetry] = useState<Telemetry>({
    cpuTemp: 34.2,
    batteryTemp: 31.8,
    ramFreeGb: 9.4,
    zramFreeGb: 12.0,
    primeFreqMhz: 2841,
    peltierLocked: true,
    activeCore: 'Core 7 (Cortex-X1 @ 2.84 GHz)',
    npuTops: 26.0,
    npuStatus: 'FastRPC Active (/dev/msm_fastrpc-2)',
    gpuGflops: 39.88,
    gpuStatus: 'Turnip Vulkan 1.3 UMA',
    inferencesCount: 1420,
    lastThroughputTps: 159.2,
    rearDisplayMsg: 'TRI-SILICON ON',
    networkIface: 'rndis0 (USB-C)',
    ipAddress: '192.168.1.10',
    powerGovernor: 'performance',
  });

  const [activeTab, setActiveTab] = useState<'console' | 'silicon' | 'display' | 'guardian'>('console');
  const [isBenchmarking, setIsBenchmarking] = useState(false);
  const [terminalLogs, setTerminalLogs] = useState<string[]>([
    '[INIT] LupinOS Bare-Metal Layer Bootstrapped on Slot B',
    '[GUARD] AMOLED Main Backlight set to 0 (power lock engaged)',
    '[GUARD] 12.0 GB ZRAM provisioned with swap priority 100',
    '[DSP] /dev/msm_fastrpc-2 attached to domain 3 (Hexagon 780 cDSP)',
    '[VULKAN] Adreno 660 initialized via Mesa Turnip 24.3.0 UMA',
    '[DISP] card0-DSI-2 secondary AMOLED HUD active (126x294 landscape buffer)',
    '[READY] Heterogeneous Inference Daemon listening on port 8080',
  ]);

  const [rearCustomText, setRearCustomText] = useState('FENRIR TITAN');

  // Simulated telemetry tick
  useEffect(() => {
    const interval = setInterval(() => {
      setTelemetry(prev => ({
        ...prev,
        cpuTemp: parseFloat((33.5 + Math.random() * 2.5).toFixed(1)),
        batteryTemp: parseFloat((31.2 + Math.random() * 1.2).toFixed(1)),
        inferencesCount: prev.inferencesCount + (isBenchmarking ? 15 : 1),
      }));
    }, 1500);
    return () => clearInterval(interval);
  }, [isBenchmarking]);

  const runBenchmark = (target: 'x1' | 'gpu' | 'npu') => {
    setIsBenchmarking(true);
    const targetName = target === 'x1' ? 'Cortex-X1 NEON RMSNorm' : target === 'gpu' ? 'Adreno 660 Vulkan GEMM' : 'Hexagon 780 cDSP DMA-BUF';
    
    setTerminalLogs(prev => [
      ...prev,
      `>>> [BENCHMARK STARTED] Executing hardware workload on ${targetName}...`,
    ]);

    setTimeout(() => {
      let result = '';
      if (target === 'x1') {
        result = '⚡ [Cortex-X1] 1000 tokens processed in 1.16 ms (1.16 µs/token, ~862,000 TPS single-thread)';
      } else if (target === 'gpu') {
        result = '🎮 [Adreno 660] 512x512 FP32 GEMM completed in 6.73 ms -> 39.88 GFLOPS via Zero-Copy UMA';
      } else {
        result = '🧠 [Hexagon 780] 1MB DMA-BUF zero-copy tensor dispatched over FastRPC socket (26 TOPS active)';
      }

      setTerminalLogs(prev => [...prev, result, '>>> [BENCHMARK FINISHED] Target verified without thermal throttling (Peltier active).']);
      setIsBenchmarking(false);
      setTelemetry(prev => ({
        ...prev,
        inferencesCount: prev.inferencesCount + 50,
        lastThroughputTps: target === 'x1' ? 862.0 : 159.2,
      }));
    }, 1200);
  };

  const toggleGovernor = (gov: 'powersave' | 'performance') => {
    setTelemetry(prev => ({ ...prev, powerGovernor: gov, primeFreqMhz: gov === 'powersave' ? 300 : 2841 }));
    setTerminalLogs(prev => [
      ...prev,
      `[POWER] Applied lupin_power_${gov === 'powersave' ? 'idle' : 'burst'}(): Core 0-6 ${gov}, Core 7 @ ${gov === 'powersave' ? '300MHz' : '2.84GHz'}`,
    ]);
  };

  const updateRearDisplay = () => {
    setTelemetry(prev => ({ ...prev, rearDisplayMsg: rearCustomText }));
    setTerminalLogs(prev => [
      ...prev,
      `[DRM_KMS] Written custom framebuffer frame to card0-DSI-2: "${rearCustomText}" (backlight 180/255)`,
    ]);
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col font-sans">
      {/* Top Bar Header */}
      <header className="border-b border-slate-800 bg-slate-900/60 backdrop-blur px-6 py-3.5 flex items-center justify-between gap-6">
        <div className="flex items-center gap-3">
          <div className="w-9 h-9 rounded-lg bg-cyan-500/10 border border-cyan-500/30 flex items-center justify-center text-cyan-400 font-bold text-lg">
            🐺
          </div>
          <div>
            <div className="text-sm font-semibold tracking-wide text-white flex items-center gap-2">
              LupinOS Extreme Silicon Control
              <span className="text-xs px-2 py-0.5 rounded bg-cyan-500/10 border border-cyan-500/20 text-cyan-400 font-mono">
                v0.5.0-star
              </span>
            </div>
            <div className="text-xs text-slate-400 font-mono">
              Xiaomi Mi 11 Ultra · Snapdragon 888 (SM8350) · Bare-Metal Chroot
            </div>
          </div>
        </div>

        {/* Live Cluster Quick Badges */}
        <div className="hidden md:flex items-center gap-4 text-xs font-mono">
          <div className="flex items-center gap-1.5 px-3 py-1 rounded bg-slate-800/80 border border-slate-700">
            <Radio className="w-3.5 h-3.5 text-emerald-400 animate-pulse" />
            <span className="text-slate-400">IP:</span>
            <span className="text-slate-200">{telemetry.ipAddress}</span>
          </div>
          <div className="flex items-center gap-1.5 px-3 py-1 rounded bg-slate-800/80 border border-slate-700">
            <Flame className="w-3.5 h-3.5 text-cyan-400" />
            <span className="text-slate-400">SoC / Peltier:</span>
            <span className="text-cyan-300">{telemetry.cpuTemp}°C</span>
          </div>
          <div className="flex items-center gap-1.5 px-3 py-1 rounded bg-slate-800/80 border border-slate-700">
            <HardDrive className="w-3.5 h-3.5 text-purple-400" />
            <span className="text-slate-400">ZRAM:</span>
            <span className="text-purple-300">12.0 GB (@100)</span>
          </div>
        </div>
      </header>

      {/* Main Container */}
      <div className="flex-1 max-w-7xl w-full mx-auto p-6 flex flex-col gap-6">
        
        {/* Navigation Tabs */}
        <div className="flex items-center gap-2 border-b border-slate-800 pb-2">
          <button 
            onClick={() => setActiveTab('console')}
            className={`px-4 py-2 text-xs font-medium rounded-lg transition-all flex items-center gap-2 ${activeTab === 'console' ? 'bg-cyan-500/10 border border-cyan-500/30 text-cyan-400' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-900'}`}
          >
            <Activity className="w-4 h-4" /> Telemetry & Compute
          </button>
          <button 
            onClick={() => setActiveTab('silicon')}
            className={`px-4 py-2 text-xs font-medium rounded-lg transition-all flex items-center gap-2 ${activeTab === 'silicon' ? 'bg-cyan-500/10 border border-cyan-500/30 text-cyan-400' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-900'}`}
          >
            <Cpu className="w-4 h-4" /> Qualcomm Silicon Units
          </button>
          <button 
            onClick={() => setActiveTab('display')}
            className={`px-4 py-2 text-xs font-medium rounded-lg transition-all flex items-center gap-2 ${activeTab === 'display' ? 'bg-cyan-500/10 border border-cyan-500/30 text-cyan-400' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-900'}`}
          >
            <Monitor className="w-4 h-4" /> Rear AMOLED HUD
          </button>
          <button 
            onClick={() => setActiveTab('guardian')}
            className={`px-4 py-2 text-xs font-medium rounded-lg transition-all flex items-center gap-2 ${activeTab === 'guardian' ? 'bg-cyan-500/10 border border-cyan-500/30 text-cyan-400' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-900'}`}
          >
            <ShieldCheck className="w-4 h-4" /> Native Guardian & Power
          </button>
        </div>

        {/* Tab 1: Telemetry & Compute */}
        {activeTab === 'console' && (
          <div className="space-y-6">
            {/* KPI Cards */}
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
              <div className="p-4 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
                <div className="flex items-center justify-between text-slate-400 text-xs">
                  <span>Prime Core 7 (Cortex-X1)</span>
                  <Cpu className="w-4 h-4 text-cyan-400" />
                </div>
                <div className="mt-3">
                  <div className="text-2xl font-bold font-mono text-white">{telemetry.primeFreqMhz} <span className="text-sm font-normal text-slate-400">MHz</span></div>
                  <div className="text-xs text-emerald-400 mt-1 flex items-center gap-1 font-mono">
                    <CheckCircle2 className="w-3 h-3" /> Pinned via sched_setaffinity
                  </div>
                </div>
              </div>

              <div className="p-4 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
                <div className="flex items-center justify-between text-slate-400 text-xs">
                  <span>Thermal Envelope (Peltier)</span>
                  <Flame className="w-4 h-4 text-amber-400" />
                </div>
                <div className="mt-3">
                  <div className="text-2xl font-bold font-mono text-cyan-300">{telemetry.cpuTemp}°C <span className="text-sm font-normal text-slate-400">/ 35°C Bat</span></div>
                  <div className="text-xs text-slate-400 mt-1 font-mono">
                    28 Thermal Zones Monitored
                  </div>
                </div>
              </div>

              <div className="p-4 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
                <div className="flex items-center justify-between text-slate-400 text-xs">
                  <span>Unified Memory (UMA)</span>
                  <HardDrive className="w-4 h-4 text-purple-400" />
                </div>
                <div className="mt-3">
                  <div className="text-2xl font-bold font-mono text-purple-300">{telemetry.ramFreeGb} <span className="text-sm font-normal text-slate-400">GB Free / 12G</span></div>
                  <div className="text-xs text-slate-400 mt-1 font-mono">
                    + 12.0 GB LZ4 ZRAM Swap
                  </div>
                </div>
              </div>

              <div className="p-4 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
                <div className="flex items-center justify-between text-slate-400 text-xs">
                  <span>Heterogeneous Inferences</span>
                  <Zap className="w-4 h-4 text-yellow-400" />
                </div>
                <div className="mt-3">
                  <div className="text-2xl font-bold font-mono text-white">{telemetry.inferencesCount.toLocaleString()} <span className="text-sm font-normal text-slate-400">reqs</span></div>
                  <div className="text-xs text-emerald-400 mt-1 font-mono">
                    Throughput: {telemetry.lastThroughputTps} tps
                  </div>
                </div>
              </div>
            </div>

            {/* Quick Benchmark Dispatch Bar */}
            <div className="p-4 rounded-xl bg-slate-900/40 border border-slate-800 flex flex-wrap items-center justify-between gap-4">
              <div>
                <div className="text-sm font-semibold text-white">Direct Silicon Benchmark Trigger</div>
                <div className="text-xs text-slate-400">Trigger bare-metal SIMD, Vulkan GEMM or FastRPC kernels directly.</div>
              </div>
              <div className="flex items-center gap-2">
                <button 
                  disabled={isBenchmarking}
                  onClick={() => runBenchmark('x1')}
                  className="px-3.5 py-1.5 text-xs font-medium rounded-lg bg-cyan-600 hover:bg-cyan-500 text-white transition-all flex items-center gap-1.5 disabled:opacity-50"
                >
                  <Play className="w-3.5 h-3.5" /> Bench Cortex-X1
                </button>
                <button 
                  disabled={isBenchmarking}
                  onClick={() => runBenchmark('gpu')}
                  className="px-3.5 py-1.5 text-xs font-medium rounded-lg bg-purple-600 hover:bg-purple-500 text-white transition-all flex items-center gap-1.5 disabled:opacity-50"
                >
                  <Play className="w-3.5 h-3.5" /> Bench Adreno GPU
                </button>
                <button 
                  disabled={isBenchmarking}
                  onClick={() => runBenchmark('npu')}
                  className="px-3.5 py-1.5 text-xs font-medium rounded-lg bg-emerald-600 hover:bg-emerald-500 text-white transition-all flex items-center gap-1.5 disabled:opacity-50"
                >
                  <Play className="w-3.5 h-3.5" /> Bench Hexagon NPU
                </button>
              </div>
            </div>

            {/* Live Terminal Output Console */}
            <div className="rounded-xl bg-slate-950 border border-slate-800 overflow-hidden font-mono text-xs">
              <div className="bg-slate-900/80 px-4 py-2 border-b border-slate-800 flex items-center justify-between">
                <div className="flex items-center gap-2 text-slate-400">
                  <Terminal className="w-4 h-4 text-cyan-400" />
                  <span>/dev/kmsg & Lupin Guardian Daemon Stream</span>
                </div>
                <div className="flex items-center gap-1">
                  <div className="w-2.5 h-2.5 rounded-full bg-emerald-500"></div>
                  <span className="text-[10px] text-slate-400">LIVE</span>
                </div>
              </div>
              <div className="p-4 space-y-1.5 max-h-64 overflow-y-auto text-slate-300">
                {terminalLogs.map((log, i) => (
                  <div key={i} className="flex gap-2 leading-relaxed">
                    <span className="text-slate-600 select-none">[{i+1}]</span>
                    <span className={log.startsWith('>>>') ? 'text-yellow-400 font-semibold' : log.startsWith('⚡') || log.startsWith('🎮') || log.startsWith('🧠') ? 'text-cyan-300 font-semibold' : 'text-slate-300'}>{log}</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* Tab 2: Qualcomm Silicon Architecture */}
        {activeTab === 'silicon' && (
          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div className="p-5 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
              <div>
                <div className="w-10 h-10 rounded-lg bg-cyan-500/10 border border-cyan-500/20 text-cyan-400 flex items-center justify-center font-bold mb-3">
                  X1
                </div>
                <div className="text-base font-semibold text-white">Prime Core Kryo 680</div>
                <div className="text-xs text-slate-400 font-mono mt-0.5">ARM Cortex-X1 @ 2.84 GHz</div>
                <p className="text-xs text-slate-300 mt-3 leading-relaxed">
                  Dedicado a single-token latency e operações não-paralelizáveis (RMSNorm e SwiGLU). Vetorização agressiva via <code className="text-cyan-300">&lt;arm_neon.h&gt;</code> com suporte a instruções <code className="text-cyan-300">sdot</code> (ARMv8.4-A).
                </p>
              </div>
              <div className="mt-4 pt-4 border-t border-slate-800/80 font-mono text-xs space-y-1.5 text-slate-400">
                <div className="flex justify-between"><span>Affinity:</span> <span className="text-white">CPU Core 7</span></div>
                <div className="flex justify-between"><span>Instruction set:</span> <span className="text-white">AArch64 + DotProd</span></div>
                <div className="flex justify-between"><span>RMSNorm Latency:</span> <span className="text-emerald-400">1.16 µs / token</span></div>
              </div>
            </div>

            <div className="p-5 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
              <div>
                <div className="w-10 h-10 rounded-lg bg-purple-500/10 border border-purple-500/20 text-purple-400 flex items-center justify-center font-bold mb-3">
                  GPU
                </div>
                <div className="text-base font-semibold text-white">Adreno 660 (840 MHz)</div>
                <div className="text-xs text-slate-400 font-mono mt-0.5">Mesa Turnip Vulkan 1.3</div>
                <p className="text-xs text-slate-300 mt-3 leading-relaxed">
                  Dedicado à ingestão massiva de prompt (Prefill phase). Acesso direto ao driver <code className="text-purple-300">/dev/kgsl-3d0</code> sem passar pelo Android HAL, usufruindo da largura de banda de 51.2 GB/s da memória LPDDR5.
                </p>
              </div>
              <div className="mt-4 pt-4 border-t border-slate-800/80 font-mono text-xs space-y-1.5 text-slate-400">
                <div className="flex justify-between"><span>Kernel Driver:</span> <span className="text-white">kgsl-3d0</span></div>
                <div className="flex justify-between"><span>Memory Type:</span> <span className="text-white">Host-Visible UMA</span></div>
                <div className="flex justify-between"><span>FP32 Throughput:</span> <span className="text-emerald-400">~39.88 GFLOPS</span></div>
              </div>
            </div>

            <div className="p-5 rounded-xl bg-slate-900/60 border border-slate-800 flex flex-col justify-between">
              <div>
                <div className="w-10 h-10 rounded-lg bg-emerald-500/10 border border-emerald-500/20 text-emerald-400 flex items-center justify-center font-bold mb-3">
                  NPU
                </div>
                <div className="text-base font-semibold text-white">Hexagon 780 cDSP</div>
                <div className="text-xs text-slate-400 font-mono mt-0.5">Qualcomm FastRPC v68 HTP</div>
                <p className="text-xs text-slate-300 mt-3 leading-relaxed">
                  26 TOPS de computação neural INT8/INT16 operando a apenas 2-3 Watts. Comunicação nativa via <code className="text-emerald-300">/dev/msm_fastrpc-2</code> carregando os blobs ELF <code className="text-emerald-300">libcdsprpc.so</code> com DMA-BUF zero-copy.
                </p>
              </div>
              <div className="mt-4 pt-4 border-t border-slate-800/80 font-mono text-xs space-y-1.5 text-slate-400">
                <div className="flex justify-between"><span>Device Node:</span> <span className="text-white">/dev/msm_fastrpc-2</span></div>
                <div className="flex justify-between"><span>Domain:</span> <span className="text-white">3 (cDSP)</span></div>
                <div className="flex justify-between"><span>Compute Peak:</span> <span className="text-emerald-400">26.0 TOPS</span></div>
              </div>
            </div>
          </div>
        )}

        {/* Tab 3: Rear AMOLED HUD */}
        {activeTab === 'display' && (
          <div className="grid grid-cols-1 md:grid-cols-2 gap-8 items-center">
            <div className="p-6 rounded-xl bg-slate-900/60 border border-slate-800 space-y-4">
              <div className="text-base font-semibold text-white">Controle do Sub-Display Traseiro (card0-DSI-2)</div>
              <p className="text-xs text-slate-300 leading-relaxed">
                O painel traseiro do Xiaomi Mi 11 Ultra possui 1.1 polegadas com resolução física portrait de <span className="font-mono text-cyan-300">126x294 pixels</span>. O driver nativo em C (<code className="text-cyan-300">drm_subdisplay.c</code>) mapeia os buffers lógicos landscape (294x126) sem depender do SurfaceFlinger ou pilhas gráficas do Android.
              </p>
              
              <div className="space-y-3 pt-2">
                <label className="text-xs font-mono text-slate-400 block">Mensagem Customizada no Header HUD:</label>
                <div className="flex gap-2">
                  <input 
                    type="text" 
                    value={rearCustomText} 
                    onChange={e => setRearCustomText(e.target.value)}
                    className="flex-1 px-3 py-2 bg-slate-950 border border-slate-800 rounded-lg text-xs font-mono text-white focus:outline-none focus:border-cyan-500"
                    placeholder="Ex: FENRIR TITAN"
                  />
                  <button 
                    onClick={updateRearDisplay}
                    className="px-4 py-2 bg-cyan-600 hover:bg-cyan-500 text-white rounded-lg text-xs font-medium transition-colors"
                  >
                    Transmitir
                  </button>
                </div>
              </div>

              <div className="pt-4 border-t border-slate-800 font-mono text-xs space-y-2 text-slate-400">
                <div className="flex justify-between"><span>Backlight:</span> <span className="text-white">180 / 255 (panel1-backlight)</span></div>
                <div className="flex justify-between"><span>Double Buffering:</span> <span className="text-emerald-400">DRM_IOCTL_MODE_PAGE_FLIP</span></div>
                <div className="flex justify-between"><span>Connector ID:</span> <span className="text-white">64 (CRTC 208)</span></div>
              </div>
            </div>

            {/* Visual Display Mockup mimicking the rear camera island OLED */}
            <div className="flex flex-col items-center">
              <div className="text-xs font-mono text-slate-400 mb-2">Simulação Fiel do Painel Traseiro (126x294):</div>
              <div className="p-3 bg-slate-900 border-4 border-slate-800 rounded-2xl shadow-2xl">
                <div 
                  className="w-[126px] h-[294px] bg-[#070c18] border border-cyan-500/40 rounded p-2 flex flex-col justify-between font-mono text-[9px] text-white selection:bg-none"
                  style={{ imageRendering: 'pixelated' }}
                >
                  <div>
                    <div className="border-b border-cyan-400/30 pb-1 text-cyan-300 font-bold tracking-wider truncate">
                      {telemetry.rearDisplayMsg}
                    </div>
                    <div className="text-[8px] text-slate-400 mt-0.5 truncate">{telemetry.ipAddress}</div>

                    <div className="mt-3 space-y-2">
                      <div>
                        <span className="text-amber-400 block text-[8px]">CORE 7 (X1):</span>
                        <span className="text-slate-100 font-bold">46.2 GFLOPS</span>
                      </div>

                      <div>
                        <span className="text-cyan-400 block text-[8px]">TEMPERATURA:</span>
                        <div className="text-slate-100">CPU:{telemetry.cpuTemp}°C</div>
                        <div className="text-emerald-400">BAT:{telemetry.batteryTemp}°C</div>
                      </div>

                      <div>
                        <span className="text-purple-400 block text-[8px]">MEMORIA (24G):</span>
                        <div className="text-slate-100">RAM: {telemetry.ramFreeGb}G LV</div>
                        <div className="text-purple-300">ZRM: 12.0G LV</div>
                      </div>

                      <div>
                        <span className="text-emerald-400 block text-[8px]">HEXAGON NPU:</span>
                        <div className="text-slate-100">26 TOPS @ 2W</div>
                      </div>
                    </div>
                  </div>

                  <div className="border-t border-cyan-500/40 pt-1 text-center text-cyan-400 font-bold text-[8px]">
                    TRI-SILICON ON
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Tab 4: Native Guardian & Power */}
        {activeTab === 'guardian' && (
          <div className="space-y-6">
            <div className="p-5 rounded-xl bg-slate-900/60 border border-slate-800">
              <div className="text-base font-semibold text-white">Governor de CPU e Controle Soberano de Energia</div>
              <div className="text-xs text-slate-400 mt-1">
                Ajuste fino de frequências e energias via sysfs (/sys/devices/system/cpu/cpufreq).
              </div>

              <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 mt-6">
                <div className={`p-4 rounded-xl border transition-all ${telemetry.powerGovernor === 'powersave' ? 'bg-cyan-500/10 border-cyan-500/40' : 'bg-slate-900 border-slate-800'}`}>
                  <div className="flex items-center justify-between">
                    <span className="text-sm font-semibold text-white">Modo Idle Profundo (Powersave)</span>
                    <button 
                      onClick={() => toggleGovernor('powersave')}
                      className={`px-3 py-1 rounded text-xs font-medium ${telemetry.powerGovernor === 'powersave' ? 'bg-cyan-500 text-black' : 'bg-slate-800 hover:bg-slate-700 text-white'}`}
                    >
                      {telemetry.powerGovernor === 'powersave' ? 'Ativo' : 'Ativar'}
                    </button>
                  </div>
                  <p className="text-xs text-slate-400 mt-2">
                    Cores 0-3 (A55) em 300MHz, Cores 4-6 (A78) e Core 7 (X1) forçados ao consumo mínimo. Ideal para repouso térmico sob bateria.
                  </p>
                </div>

                <div className={`p-4 rounded-xl border transition-all ${telemetry.powerGovernor === 'performance' ? 'bg-cyan-500/10 border-cyan-500/40' : 'bg-slate-900 border-slate-800'}`}>
                  <div className="flex items-center justify-between">
                    <span className="text-sm font-semibold text-white">Modo Monstro (Burst @ 2.84 GHz)</span>
                    <button 
                      onClick={() => toggleGovernor('performance')}
                      className={`px-3 py-1 rounded text-xs font-medium ${telemetry.powerGovernor === 'performance' ? 'bg-cyan-500 text-black' : 'bg-slate-800 hover:bg-slate-700 text-white'}`}
                    >
                      {telemetry.powerGovernor === 'performance' ? 'Ativo' : 'Ativar'}
                    </button>
                  </div>
                  <p className="text-xs text-slate-400 mt-2">
                    Core 7 Cortex-X1 fixado no clock máximo absoluto de 2.84 GHz com CPU affinity. Requer Peltier ativo (&lt;65°C).
                  </p>
                </div>
              </div>
            </div>

            <div className="p-5 rounded-xl bg-slate-900/60 border border-slate-800">
              <div className="text-base font-semibold text-white">Proteções Operacionais do Lupin Native Guardian</div>
              <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 mt-4 font-mono text-xs">
                <div className="p-3 bg-slate-950 rounded-lg border border-slate-800">
                  <div className="text-slate-400">AMOLED Panel 0</div>
                  <div className="text-emerald-400 font-bold mt-1">BRIGHTNESS: 0</div>
                  <div className="text-[10px] text-slate-500 mt-1">Prevenção total de burn-in</div>
                </div>

                <div className="p-3 bg-slate-950 rounded-lg border border-slate-800">
                  <div className="text-slate-400">USB Autosuspend</div>
                  <div className="text-emerald-400 font-bold mt-1">DISABLED (-1)</div>
                  <div className="text-[10px] text-slate-500 mt-1">Wakelock "lupinos_usb"</div>
                </div>

                <div className="p-3 bg-slate-950 rounded-lg border border-slate-800">
                  <div className="text-slate-400">Watchdog de Rede</div>
                  <div className="text-emerald-400 font-bold mt-1">AUTO-FAILOVER</div>
                  <div className="text-[10px] text-slate-500 mt-1">eth0 / rndis0 / wlan0</div>
                </div>
              </div>
            </div>
          </div>
        )}
      </div>

      {/* Footer */}
      <footer className="border-t border-slate-800/80 px-6 py-4 text-xs font-mono text-slate-500 flex flex-wrap justify-between items-center gap-4">
        <div>LupinOS Extreme Silicon Layer · Bare-Metal Snapdragon 888 Infrastructure</div>
        <div className="flex items-center gap-4">
          <span>Hexagon 780 (26 TOPS)</span>
          <span>·</span>
          <span>Adreno 660 UMA</span>
          <span>·</span>
          <span>Cortex-X1 @ 2.84GHz</span>
        </div>
      </footer>
    </div>
  );
}
