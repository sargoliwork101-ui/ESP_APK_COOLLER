#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

// =============================================================
//  webpage.h  —  ظاهر پنل (بازنویسی شده دقیقاً مطابق فایل گالری طرح‌ها)
//  منطق برنامه (مسیرهای سرور، فیلدهای فرم، رله، سناریو، NTP، Watchdog)
//  تغییری نکرده است؛ فقط CSS / چیدمان / کلاس‌ها مطابق گالری بازنویسی شده‌اند.
//  نام برنامه و برند از داخل اسکچ (.ino) با ثابت‌های
//  PROGRAM_NAME و PROGRAM_TAGLINE در جایگزین‌های
//  %%PROGRAM_NAME%% و %%PROGRAM_TAGLINE%% جای‌گذاری می‌شوند.
// =============================================================

const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>%%PROGRAM_NAME%%</title>

  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Vazirmatn:wght@300;400;500;600;700;800&display=swap" rel="stylesheet">

  <style>
    /* ===== پالت رنگی گالری (طرح a) ===== */
    :root{
      --font-fa:'Vazirmatn',-apple-system,sans-serif;
      --font-mono:'Vazirmatn',-apple-system,sans-serif;
      --bg:#0a0c0d; --panel:#1b1f21; --panel-2:#15181a; --line:rgba(255,255,255,.08); --line-strong:rgba(255,255,255,.14);
      --accent:#1de9c4; --accent-dim:rgba(29,233,196,.16); --on-accent:#00201b;
      --on:#1de9c4; --on-dim:rgba(29,233,196,.16); --off:#ff5468; --off-dim:rgba(255,84,104,.14);
      --warn:#f2a93b; --warn-dim:rgba(242,169,59,.14); --cyan:#29d3c8; --on-cyan:#04201d;
      --scenario-live:#3ddc84; --scenario-live-dim:rgba(61,220,132,.14); --scenario-next:#f5d020; --scenario-next-dim:rgba(245,208,32,.16);
      --text:#eef0f1; --text-dim:#868c90; --text-faint:#585c5f; --nav-bg:rgba(15,18,19,.92); --panel-radius:14px; --btn-radius:12px;
    }
    *{box-sizing:border-box; -webkit-tap-highlight-color:transparent;}
    body{margin:0; background:#000; color:var(--text); font-family:var(--font-fa); display:flex; flex-direction:column; align-items:center;}

    /* ===== فریم گوشی ===== */
    .phone{width:100%; min-height:100vh; background:var(--bg); color:var(--text); display:flex; position:relative; border-left:1px solid var(--line); border-right:1px solid var(--line);}
    .phone.col{flex-direction:column;}
    .phone.row{flex-direction:row;}
    .main-col{flex:1; display:flex; flex-direction:column; min-width:0;}

    /* ===== نوار بالا ===== */
    .topbar{padding:16px 18px; position:sticky; top:0; z-index:20; background:var(--bg); backdrop-filter:blur(14px); -webkit-backdrop-filter:blur(14px); border-bottom:1px solid var(--line);}
    .top-row{display:flex; align-items:center; justify-content:space-between;}
    .brand-name{font-weight:800; font-size:1rem;}
    .brand-sub{font-family:var(--font-mono); font-size:.66rem; color:var(--text-faint); letter-spacing:.4px;}
    .conn-badge{display:flex; align-items:center; gap:6px; padding:6px 12px; border-radius:99px; font-size:.7rem; font-weight:700; background:var(--accent-dim); color:var(--accent); font-family:var(--font-mono);}
    .conn-badge .dot{width:6px; height:6px; border-radius:50%; background:var(--accent); box-shadow:0 0 8px var(--accent); animation:pulse 1.8s infinite;}
    @keyframes pulse{0%,100%{opacity:1;}50%{opacity:.35;}}
    .conn-badge.connected{background:var(--on-dim); color:var(--on); box-shadow:0 0 8px var(--on-dim);}
    .conn-badge.connected .dot{background:var(--on); box-shadow:0 0 10px var(--on);}
    .conn-badge.disconnected{background:var(--off-dim); color:var(--off); box-shadow:0 0 8px var(--off-dim);}
    .conn-badge.disconnected .dot{background:var(--off); box-shadow:0 0 10px var(--off);}
    .conn-badge.connecting{background:var(--warn-dim); color:var(--warn); box-shadow:0 0 8px var(--warn-dim);}
    .conn-badge.connecting .dot{background:var(--warn); box-shadow:0 0 10px var(--warn);}

    /* ===== نشانگر وضعیت سه‌حالته (نارنجی=درحال اتصال، سبز=وصل، قرمز=قطع) برای متن‌های وضعیت اتصال ===== */
    .status-connecting{color:var(--warn)!important;}
    .status-connected{color:var(--scenario-live)!important;}
    .status-disconnected{color:var(--off)!important;}

    /* ===== نوتیفیکیشن کوتاه (Toast) برای اطلاع فوری قطع/وصل شدن مودم، بدون نیاز به تایید کاربر ===== */
    #toast-wrap{position:fixed; top:14px; left:50%; transform:translateX(-50%); z-index:10500; display:flex; flex-direction:column; gap:8px; align-items:center; width:92%; max-width:400px; pointer-events:none;}
    .toast{padding:11px 18px; border-radius:12px; font-size:.82rem; font-weight:700; text-align:center; box-shadow:0 10px 26px rgba(0,0,0,.45); backdrop-filter:blur(10px); -webkit-backdrop-filter:blur(10px); opacity:0; transform:translateY(-10px); transition:opacity .25s ease, transform .25s ease; max-width:100%;}
    .toast.show{opacity:1; transform:translateY(0);}
    .toast.toast-warn{background:var(--warn-dim); color:var(--warn); border:1px solid var(--warn);}
    .toast.toast-good{background:var(--on-dim); color:var(--on); border:1px solid var(--on);}
    .toast.toast-bad{background:var(--off-dim); color:var(--off); border:1px solid var(--off);}

    /* ===== آمار سوییچ / مدت‌کارکرد رله (پایش سلامت کمپرسور) ===== */
    .stat-row{display:flex; gap:8px;}
    .stat-box{flex:1; background:var(--panel-2); border-radius:9px; padding:12px 10px; text-align:center; box-shadow:4px 4px 10px rgba(0,0,0,.45), -3px -3px 8px rgba(255,255,255,.03);}
    .stat-value{font-family:var(--font-mono); font-size:1.15rem; font-weight:800; color:var(--accent);}
    .stat-lbl{font-size:.68rem; color:var(--text-dim); margin-top:4px;}

    /* ===== صفحات (تب‌ها) ===== */
    .pages{flex:1; padding:20px 18px 100px; width:100%;}
    .page{display:none; animation:fadeIn .3s ease;}
    .page.active{display:block;}
    @keyframes fadeIn{from{opacity:0; transform:translateY(6px);}to{opacity:1; transform:translateY(0);}}

    /* ===== کارت / پنل (سبک نئومورف گالری) ===== */
    .panel{position:relative; background:var(--panel); border:1px solid var(--line); border-radius:var(--panel-radius); padding:18px; margin-bottom:14px; box-shadow:7px 7px 15px rgba(0,0,0,.5), -5px -5px 11px rgba(255,255,255,.03);}
    .panel.rivets::before,.panel.rivets::after{content:''; position:absolute; top:9px; width:4px; height:4px; border-radius:50%; background:var(--line-strong);}
    .panel.rivets::before{right:9px;} .panel.rivets::after{left:9px;}
    .panel-label{display:flex; align-items:center; justify-content:space-between; margin-bottom:14px;}
    .panel-label h3{margin:0; font-size:.8rem; font-weight:700; color:var(--text-dim); letter-spacing:.3px; display:flex; align-items:center; gap:8px;}
    .panel-label h3.bar::before{content:''; width:3px; height:14px; background:var(--accent); border-radius:2px; display:inline-block;}

    /* ===== هیرو (حلقه + فن) ===== */
    .hero{display:flex; flex-direction:column; align-items:center; padding:22px 18px 20px;}
    .ring-wrap{position:relative; width:126px; height:126px; margin-bottom:14px;}
    .ring-wrap svg.ring{width:100%; height:100%; transform:rotate(-90deg);}
    .ring-bg{fill:none; stroke:var(--line); stroke-width:5;}
    .ring-fg{fill:none; stroke:var(--on); stroke-width:5; stroke-linecap:round; stroke-dasharray:380; stroke-dashoffset:55; filter:drop-shadow(0 0 5px var(--on));}
    .ring-center{position:absolute; inset:0; display:flex; align-items:center; justify-content:center;}
    .fan-icon{width:36px; height:36px; color:var(--on);}
    .fan-icon .blade-group{transform-origin:12px 12px; animation:spin 1.1s linear infinite;}
    @keyframes spin{100%{transform:rotate(360deg);}}

    /* تصویر اختصاصی کولر: بدنه سه‌بعدی، پرهٔ متحرک و جریان هوای نورانی */
    .ring-wrap{width:156px;height:156px;margin-bottom:10px;}
    .ring{opacity:.55;}
    .cooler-art{width:146px;height:146px;overflow:visible;}
    .cooler-art .cooler-body{fill:#222a2d;stroke:#506065;stroke-width:1.3;filter:drop-shadow(0 12px 10px rgba(0,0,0,.52));}
    .cooler-art .cooler-top{fill:#303c40;stroke:#647277;stroke-width:1;}
    .cooler-art .grille{fill:#111719;stroke:#5c6a6d;stroke-width:1.4;}
    .cooler-art .grille-line{stroke:#526166;stroke-width:1.2;opacity:.85;}
    .cooler-art .fan-blades{transform-origin:73px 72px;animation:coolerSpin 1.15s linear infinite;}
    .cooler-art .blade{fill:var(--on);filter:drop-shadow(0 0 3px var(--on));}
    .cooler-art .hub{fill:#b9fff2;stroke:#0d8e78;stroke-width:2;}
    .cooler-art .air{fill:none;stroke:var(--on);stroke-width:2.1;stroke-linecap:round;opacity:.78;filter:drop-shadow(0 0 4px var(--on));animation:airFlow 1.4s ease-in-out infinite;}
    .cooler-art .air.a2{animation-delay:.25s}.cooler-art .air.a3{animation-delay:.5s}
    .cooler-art .led{fill:var(--on);filter:drop-shadow(0 0 5px var(--on));}
    @keyframes coolerSpin{to{transform:rotate(360deg)}}
    @keyframes airFlow{0%,100%{opacity:.15;transform:translateX(-3px)}50%{opacity:1;transform:translateX(4px)}}
    .hero.off .cooler-art .fan-blades{animation:none}.hero.off .cooler-art .blade,.hero.off .cooler-art .led{fill:var(--text-faint);filter:none}.hero.off .cooler-art .air{display:none}.hero.off .cooler-art .cooler-body{fill:#202426;stroke:#3b4245;}
    .hero.disconnected .cooler-art .fan-blades{animation:none}.hero.disconnected .cooler-art .blade,.hero.disconnected .cooler-art .led{fill:var(--off);filter:none}.hero.disconnected .cooler-art .air{display:none;}
    .status-word{font-weight:800; font-size:1.02rem; margin-top:2px;}
    .status-sub{font-family:var(--font-mono); font-size:.7rem; color:var(--text-dim); margin-top:4px;}
    .manual-btn{width:100%; padding:14px; border-radius:var(--btn-radius); border:1px solid var(--off-dim); background:var(--off-dim); color:var(--off); font-family:var(--font-fa); font-weight:700; font-size:.92rem; cursor:pointer; margin-top:14px;}
    .manual-btn.isoff{border-color:var(--on-dim); background:var(--on-dim); color:var(--on);}

    /* حالت‌های هیرو بر اساس وضعیت رله / ارتباط */
    .hero.off .ring-fg{stroke:var(--text-faint); filter:none;}
    .hero.off .fan-icon{color:var(--text-faint);}
    .hero.off .fan-icon .blade-group{animation:none;}
    .hero.disconnected .ring-fg{stroke:var(--off); filter:drop-shadow(0 0 5px var(--off));}
    .hero.disconnected .fan-icon{color:var(--off);}
    .hero.disconnected .fan-icon .blade-group{animation:none;}
    .hero.disconnected .status-word{color:var(--off);}

    /* ===== ساعت داخلی برد ===== */
    .clock-row{display:flex; align-items:center; justify-content:space-between; gap:12px;}
    .clock-value{flex:1; text-align:center; font-family:var(--font-mono); font-size:1.4rem; font-weight:700; color:var(--cyan); letter-spacing:1px;}
    .sync-btn{flex-shrink:0; padding:9px 14px; border-radius:9px; border:1px solid var(--line-strong); background:var(--panel-2); color:var(--text); font-family:var(--font-fa); font-size:.78rem; font-weight:700; cursor:pointer;}
    .sync-note{margin-top:10px; font-size:.76rem; color:var(--on);}
    .date-value{text-align:center;font-size:.78rem;color:var(--text-dim);margin-top:5px;font-family:var(--font-fa);}

    .header-row{display:flex; align-items:center; justify-content:space-between; margin-bottom:14px;}
    .fab{width:32px; height:32px; border-radius:9px; background:var(--accent-dim); border:1px solid var(--accent); color:var(--accent); display:flex; align-items:center; justify-content:center; font-size:1.2rem; cursor:pointer; flex-shrink:0;}

    /* ===== کارت سناریو ===== */
    .scenario-card{position:relative; background:var(--panel-2); border-radius:12px; padding:13px; margin-bottom:9px; border:1px solid var(--line); border-right:3px solid var(--line-strong);}
    .scenario-card.running{border-right-color:var(--scenario-live); background:var(--scenario-live-dim);}
    .scenario-card.next{border-right-color:var(--scenario-next); background:var(--scenario-next-dim);}
    .scenario-card.scenario-disabled{opacity:.42; border-style:dashed;}
    .scenario-card.error-conflict{border-color:var(--off)!important; border-right-color:var(--off)!important; box-shadow:0 0 16px rgba(255,84,104,.22)!important; background:var(--off-dim)!important; animation:cardShake .45s;}
    @keyframes cardShake{0%,100%{transform:translateX(0);}20%,60%{transform:translateX(-4px);}40%,80%{transform:translateX(4px);}}
    .scenario-top{display:flex; align-items:center; justify-content:space-between; margin-bottom:11px;}
    .scenario-name{font-weight:700; font-size:.85rem; display:flex; align-items:center; gap:7px;}
    .badge-live{font-size:.6rem; padding:2px 7px; border-radius:99px; background:var(--scenario-live-dim); color:var(--scenario-live); font-family:var(--font-mono); font-weight:700;}
    .badge-next{font-size:.6rem; padding:2px 7px; border-radius:99px; background:var(--scenario-next-dim); color:var(--scenario-next); font-family:var(--font-mono); font-weight:700;}
    .scenario-actions{display:flex; align-items:center; gap:9px;}
    .icon-btn{width:26px; height:26px; border-radius:7px; border:1px solid var(--line-strong); background:var(--panel); color:var(--text-dim); display:flex; align-items:center; justify-content:center; cursor:pointer; font-size:.85rem;}
    .icon-btn.danger:hover{background:var(--off-dim); color:var(--off); border-color:var(--off);}
    .time-row{display:flex; gap:8px;}
    .time-box{flex:1; background:var(--panel); border-radius:9px; padding:9px 10px; text-align:center;}
    .time-box .lbl{font-size:.65rem; color:var(--text-faint); margin-bottom:3px;}
    .time-box input{width:100%; background:transparent; border:none; color:var(--text); font-family:var(--font-mono); font-size:1.02rem; font-weight:700; text-align:center; outline:none; direction:ltr;}
    .time-box input::-webkit-calendar-picker-indicator{filter:invert(.85); cursor:pointer;}
    #scenarios-list{display:flex; flex-direction:column;}
    .days-label{font-size:.68rem;color:var(--text-faint);margin:11px 0 6px;}
    .days-row{display:flex;gap:4px;justify-content:space-between;direction:rtl;}
    .day-btn{flex:1;min-width:0;padding:6px 1px;border-radius:7px;border:1px solid var(--line-strong);background:var(--panel);color:var(--text-dim);font-family:var(--font-fa);font-size:.66rem;font-weight:700;cursor:pointer;}
    .day-btn.selected{background:var(--accent-dim);border-color:var(--accent);color:var(--accent);}
    .power-row{display:flex;gap:6px;direction:rtl;}
    .power-btn{flex:1;min-width:0;padding:10px 4px;border-radius:9px;border:1px solid var(--line-strong);background:var(--panel-2);color:var(--text-dim);font-family:var(--font-fa);font-size:.72rem;font-weight:700;cursor:pointer;box-shadow:3px 3px 7px rgba(0,0,0,.45), -2px -2px 6px rgba(255,255,255,.03);}
    .power-btn.selected{background:var(--accent-dim);border-color:var(--accent);color:var(--accent);}
    .save-btn{width:100%; padding:14px; border-radius:var(--btn-radius); border:none; margin-top:4px; background:var(--accent); color:var(--on-accent); font-weight:800; font-size:.92rem; cursor:pointer;}

    /* ===== بنرهای اطلاع‌رسانی تب سناریوها ===== */
    .scenario-banner{background:var(--warn-dim); border:1px solid var(--warn); color:var(--warn); border-radius:10px; padding:10px 12px; font-size:.78rem; line-height:1.7; margin-bottom:12px; font-weight:600;text-align:center;}
    #manual-disabled-note{background:var(--off-dim); border-color:var(--off); color:var(--off);}
    /* پشتیبان‌گیری/بازیابی محلی سناریوها */
    .backup-row{display:flex;gap:8px;margin:-2px 0 7px;}
    .backup-btn{flex:1;padding:10px 7px;border-radius:10px;font-family:var(--font-fa);font-size:.76rem;font-weight:700;cursor:pointer;text-align:center;display:flex;align-items:center;justify-content:center;}
    .backup-btn.export{background:var(--accent-dim);border:1px solid var(--accent);color:var(--accent);}
    .backup-btn.import{background:var(--cyan);border:1px solid var(--cyan);color:var(--on-cyan);}
    .backup-note{font-size:.68rem;color:var(--text-faint);text-align:center;margin-bottom:13px;}
    .file-input{position:absolute;width:1px;height:1px;opacity:0;overflow:hidden;pointer-events:none;}

    /* ===== فرم‌های وای‌فای ===== */
    .field{margin-bottom:15px;}
    .field label{display:block; font-size:.76rem; color:var(--text-dim); margin-bottom:6px; font-weight:600;}
    .field input{width:100%; padding:12px 13px; border-radius:9px; border:1px solid var(--line-strong); background:var(--panel-2); color:var(--text); font-family:var(--font-mono); font-size:.86rem; direction:ltr; text-align:left;}
    .password-container{position:relative; display:flex; align-items:center; width:100%;}
    .password-container input{padding-left:50px!important;}
    .toggle-password{position:absolute; left:10px; background:none; border:none; color:var(--text-dim); cursor:pointer; display:flex; align-items:center; justify-content:center; padding:0; width:34px; height:34px; box-shadow:none;}
    .toggle-password:hover{color:var(--accent);}
    .hint{font-size:.76rem; color:var(--text-dim); line-height:1.8; margin-bottom:14px;}
    .wifi-btn{width:100%; padding:14px; border-radius:var(--btn-radius); border:none; background:var(--cyan); color:var(--on-cyan); font-weight:800; font-size:.92rem; cursor:pointer;}
    #internet-wifi-status{font-size:.9rem; color:var(--text-dim); margin-bottom:16px;}

    /* ===== کلید تگل (switch) ===== */
    .switch{direction:ltr; width:38px; height:21px; border-radius:99px; position:relative; cursor:pointer; flex-shrink:0; transition:.25s; background:var(--panel-2); border:1px solid var(--line-strong);}
    .switch .knob{position:absolute; top:2px; left:2px; width:15px; height:15px; border-radius:50%; background:var(--text-faint); transition:.25s;}
    .switch.on{background:var(--on-dim); border-color:var(--on);}
    .switch.on .knob{transform:translateX(17px); background:var(--on); box-shadow:0 0 6px var(--on);}

    /* ===== ناوبری شناور مدور (orbit) ===== */
    .nav-orbit{position:fixed; bottom:16px; left:50%; transform:translateX(-50%); width:calc(100% - 44px); max-width:380px; display:flex; align-items:flex-end; justify-content:space-around; background:var(--nav-bg); border:1px solid var(--line-strong); border-radius:28px; padding:10px 12px 12px; z-index:30; box-shadow:0 14px 34px rgba(0,0,0,.55);}
    .nav-orbit .nav-btn{display:flex; flex-direction:column; align-items:center; gap:4px; padding:8px; border-radius:50%; cursor:pointer; color:var(--text-faint); background:none; border:none; transition:transform .3s cubic-bezier(.34,1.56,.64,1), background .3s, color .3s, box-shadow .3s; font-family:var(--font-fa);}
    .nav-orbit .nav-btn svg{width:19px; height:19px;}
    .nav-orbit .nav-btn span{font-size:.6rem; font-weight:700;}
    .nav-orbit .nav-btn.active{color:var(--on-accent); background:var(--accent); transform:translateY(-14px) scale(1.16); box-shadow:0 10px 22px var(--accent-dim), 0 0 0 5px var(--bg);}
    .nav-orbit .nav-btn.active span{display:none;}

    /* ===== مودال هشدار و پروگرس ===== */
    #custom-alert{display:none; position:fixed; top:50%; left:50%; transform:translate(-50%,-50%); background:rgba(15,18,19,.96); border:1px solid var(--line-strong); border-radius:22px; padding:28px; z-index:10000; box-shadow:0 24px 70px rgba(0,0,0,.7); backdrop-filter:blur(32px); -webkit-backdrop-filter:blur(32px); width:90%; max-width:400px; text-align:center;}
    #custom-alert-text{color:var(--text); font-size:1.05rem; margin:0 0 24px; line-height:1.7;}
    .btn-alert-close{background:var(--accent); color:var(--on-accent); border:none; padding:12px 32px; border-radius:12px; font-weight:700; cursor:pointer;}
    #custom-alert-overlay{display:none; position:fixed; inset:0; background:rgba(0,0,0,.7); z-index:9999; backdrop-filter:blur(8px); -webkit-backdrop-filter:blur(8px);}
    #progress-modal{display:none; position:fixed; top:50%; left:50%; transform:translate(-50%,-50%); background:rgba(15,18,19,.96); border:1px solid var(--line-strong); border-radius:24px; padding:30px; z-index:10001; box-shadow:0 25px 65px rgba(0,0,0,.85); backdrop-filter:blur(32px); -webkit-backdrop-filter:blur(32px); width:90%; max-width:400px; text-align:center;}
    #progress-title{color:var(--accent); margin:0 0 15px;}
    #progress-text{color:var(--text-dim); font-size:.95rem; margin:0 0 24px; line-height:1.6;}
    #progress-bar-fill{width:0%; height:100%; background:linear-gradient(90deg,var(--accent),var(--cyan)); border-radius:6px; transition:width .15s ease;}
    #progress-percent{font-family:var(--font-mono); font-weight:700; color:var(--accent); font-size:1.25rem;}
    .progress-track{width:100%; background:rgba(255,255,255,.05); height:12px; border-radius:6px; overflow:hidden; border:1px solid var(--line-strong); margin-bottom:15px;}

    hr{border:none; border-top:1px solid var(--line); margin:22px 0;}

    /* ===== سایه‌های نئومورف (سبک rivets-neu مطابق گالری) ===== */
    .scenario-card, .time-box{box-shadow:4px 4px 10px rgba(0,0,0,.45), -3px -3px 8px rgba(255,255,255,.03);}
    .icon-btn, .field input{box-shadow:3px 3px 7px rgba(0,0,0,.45), -2px -2px 6px rgba(255,255,255,.03);}
    .manual-btn, .save-btn, .wifi-btn{box-shadow:4px 4px 10px rgba(0,0,0,.45), -3px -3px 8px rgba(255,255,255,.04);}

    @media (max-width:480px){
      .phone{border-left:none; border-right:none;}
    }
  </style>
</head>
<body>

  <div id="custom-alert-overlay"></div>
  <div id="custom-alert">
    <p id="custom-alert-text"></p>
    <button class="btn-alert-close" onclick="closeAlert()">تایید</button>
  </div>

  <div id="progress-modal">
    <h3 id="progress-title">در حال ذخیره سناریوها...</h3>
    <p id="progress-text">لطفاً منتظر بمانید، اطلاعات روی حافظه بورد نوشته می‌شود.</p>
    <div class="progress-track"><div id="progress-bar-fill"></div></div>
    <div id="progress-percent">0%</div>
  </div>

  <div class="phone col">
    <div class="topbar">
      <div class="top-row">
        <div>
          <div class="brand-name">%%PROGRAM_NAME%%</div>
          <div class="brand-sub">%%PROGRAM_TAGLINE%%</div>
        </div>
        <div class="conn-badge connecting" id="connection-status"><span class="dot"></span> در حال اتصال...</div>
      </div>
    </div>

    <div id="toast-wrap"></div>

    <div class="pages">
      <div class="page active" id="tab-home">
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
    </div><!-- /page home -->

    <div class="nav-orbit">
      <button class="nav-btn active" id="tab-btn-home" onclick="switchTab('home')" data-nav="home">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 11l9-7 9 7"/><path d="M5 10v9a1 1 0 0 0 1 1h4v-6h4v6h4a1 1 0 0 0 1-1v-9"/></svg>
        <span>خانه</span>
      </button>
      <button class="nav-btn" id="tab-btn-scenarios" onclick="switchTab('scenarios')" data-nav="scenarios">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="17" rx="2"/><path d="M8 2v4M16 2v4M3 9h18"/></svg>
        <span>سناریوها</span>
      </button>
      <button class="nav-btn" id="tab-btn-wifi" onclick="switchTab('wifi')" data-nav="wifi">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .34 1.88l.06.06-2.1 2.1-.06-.06a1.7 1.7 0 0 0-1.88-.34 1.7 1.7 0 0 0-1.03 1.56v.1h-3v-.1a1.7 1.7 0 0 0-1.03-1.56 1.7 1.7 0 0 0-1.88.34l-.06.06-2.1-2.1.06-.06A1.7 1.7 0 0 0 7.06 15a1.7 1.7 0 0 0-1.56-1.03h-.1v-3h.1A1.7 1.7 0 0 0 7.06 9.94a1.7 1.7 0 0 0-.34-1.88l-.06-.06 2.1-2.1.06.06a1.7 1.7 0 0 0 1.88.34 1.7 1.7 0 0 0 1.03-1.56v-.1h3v.1a1.7 1.7 0 0 0 1.03 1.56 1.7 1.7 0 0 0 1.88-.34l.06-.06 2.1 2.1-.06.06a1.7 1.7 0 0 0-.34 1.88 1.7 1.7 0 0 0 1.56 1.03h.1v3h-.1A1.7 1.7 0 0 0 19.4 15z"/></svg>
        <span>تنظیمات</span>
      </button>
    </div>
  </div><!-- /phone -->

  <script>
    // ===================== توابع پایه هشدار =====================
    function showModal(msg){
      document.getElementById('custom-alert-text').innerText = msg;
      document.getElementById('custom-alert-overlay').style.display = 'block';
      document.getElementById('custom-alert').style.display = 'block';
    }
    function closeAlert(){
      document.getElementById('custom-alert-overlay').style.display = 'none';
      document.getElementById('custom-alert').style.display = 'none';
    }

    function togglePasswordVisibility(){
      const passInput = document.getElementById('wifi-pass');
      const eyeIcon = document.getElementById('eye-icon');
      if(!passInput) return;
      if(passInput.type === 'password'){
        passInput.type = 'text';
        if(eyeIcon) eyeIcon.innerHTML = '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>';
      } else {
        passInput.type = 'password';
        if(eyeIcon) eyeIcon.innerHTML = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
      }
    }

    function switchTab(tabId){
      document.querySelectorAll('.nav-btn').forEach(b=>b.classList.remove('active'));
      document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
      if(tabId==='home'){ document.getElementById('tab-btn-home').classList.add('active'); document.getElementById('tab-home').classList.add('active'); }
      else if(tabId==='scenarios'){ document.getElementById('tab-btn-scenarios').classList.add('active'); document.getElementById('tab-scenarios').classList.add('active'); updateScenariosManualNote(); }
      else if(tabId==='wifi'){ document.getElementById('tab-btn-wifi').classList.add('active'); document.getElementById('tab-wifi').classList.add('active'); }
    }

    // جابه‌جایی لمسی بین تب‌ها؛ کنترل‌های فرم (زمان، دکمه و ورودی‌ها) دست‌نخورده می‌مانند.
    let swipeStartX=0, swipeStartY=0, swipeEligible=false;
    const pageOrder=['home','scenarios','wifi'];
    document.addEventListener('touchstart', function(e){
      if(e.touches.length!==1) return;
      const target=e.target;
      swipeEligible=!target.closest('input,button,.switch,.fab,.icon-btn');
      swipeStartX=e.touches[0].clientX; swipeStartY=e.touches[0].clientY;
    },{passive:true});
    document.addEventListener('touchend', function(e){
      if(!swipeEligible || !e.changedTouches.length) return;
      const dx=e.changedTouches[0].clientX-swipeStartX, dy=e.changedTouches[0].clientY-swipeStartY;
      // فقط حرکت افقی روشن و عمدی؛ اسکرول عمودی صفحه را مختل نمی‌کند.
      if(Math.abs(dx)<65 || Math.abs(dx)<Math.abs(dy)*1.4) return;
      const active=document.querySelector('.page.active');
      const current=active ? active.id.replace('tab-','') : 'home';
      const index=pageOrder.indexOf(current);
      // در رابط راست‌به‌چپ: کشیدن به چپ = صفحهٔ بعد، کشیدن به راست = صفحهٔ قبل
      const next=dx<0 ? index+1 : index-1;
      if(next>=0 && next<pageOrder.length) switchTab(pageOrder[next]);
    },{passive:true});

    function updateScenariosManualNote(){
      const note=document.getElementById('manual-disabled-note');
      if(!note) return;
      const mb=document.getElementById('manual-btn');
      const manualOn = mb && !mb.classList.contains('isoff');
      note.style.display = manualOn ? 'block' : 'none';
    }

    function syncTime(){
      let now = new Date();
      fetch('/sync', {method:'POST', body:'h='+now.getHours()+'&m='+now.getMinutes()+'&s='+now.getSeconds()+'&y='+now.getFullYear()+'&mon='+(now.getMonth()+1)+'&d='+now.getDate()+'&wd='+((now.getDay()+1)%7), headers:{'Content-Type':'application/x-www-form-urlencoded'}})
        .then(r=>showModal('ساعت داخلی دستگاه با موفقیت با گوشی شما همگام‌سازی شد.'));
    }

    function toggleManual(){
      let btn = document.getElementById('manual-btn');
      if(btn){
        btn.classList.toggle('isoff');
        btn.innerText = btn.classList.contains('isoff') ? 'روشن کردن دستی کولر' : 'خاموش کردن دستی کولر';
      }
      updateScenariosManualNote();
      fetch('/toggle-manual', {method:'POST'});
    }

    function toggleScenarioDay(index, day){
      const row=document.getElementById('row_'+index); if(!row) return;
      const input=row.querySelector('input[name="wd_'+index+'"]');
      const btn=row.querySelector('.day-btn[data-day="'+day+'"]'); if(!input || !btn) return;
      let mask=parseInt(input.value,10); if(isNaN(mask)) mask=127;
      const bit=1<<day;
      if(mask & bit){ if((mask & 127)===bit){ showModal('حداقل یک روز برای اجرای سناریو انتخاب کنید.'); return; } mask &= ~bit; }
      else mask |= bit;
      input.value=mask; btn.classList.toggle('selected',(mask&bit)!==0);
    }

    function toggleScenarioEnabled(index){
      const row = document.getElementById('row_'+index);
      if(!row) return;
      const en = row.querySelector('input[name="en_'+index+'"]');
      const sw = row.querySelector('.switch');
      if(!en || !sw) return;
      en.checked = !en.checked;
      if(en.checked){
        row.classList.remove('scenario-disabled');
        sw.classList.add('on');
      } else {
        row.classList.add('scenario-disabled');
        row.classList.remove('running','next');
        sw.classList.remove('on');
      }
      updateScenarioOrder();
    }

    function toggleNet(sw){
      const cb = document.getElementById('internet-enabled');
      if(!cb) return;
      cb.checked = !cb.checked;
      sw.classList.toggle('on', cb.checked);
    }

    function toggleApCycleSwitch(sw){
      const cb = document.getElementById('ap-cycle-enabled');
      if(!cb) return;
      cb.checked = !cb.checked;
      sw.classList.toggle('on', cb.checked);
    }

    function selectTxPower(level){
      const row=document.getElementById('tx-power-row');
      const input=document.getElementById('tx-power-value');
      if(!row || !input) return;
      input.value=level;
      row.querySelectorAll('.power-btn').forEach(btn=>btn.classList.toggle('selected', parseInt(btn.dataset.power,10)===level));
    }

    function updateScenarioOrder(){
      const rows = Array.from(document.querySelectorAll('.scenario-card'));
      rows.forEach(row=>{
        const visible = row.style.display !== 'none';
        if(!visible){ row.style.order = 10000; return; }
        const sh = row.querySelector('input[name^="sh_"]');
        const eh = row.querySelector('input[name^="eh_"]');
        let w = -1;
        if(sh && sh.value && eh && eh.value){
          const p = sh.value.split(':');
          if(p.length===2) w = parseInt(p[0])*60 + parseInt(p[1]);
        }
        row.style.order = w;
      });
      const vis = rows.filter(r=>r.style.display!=='none');
      vis.sort((a,b)=>parseInt(a.style.order)-parseInt(b.style.order));
      vis.forEach((row,idx)=>{
        const name = row.querySelector('.scenario-name');
        if(name && name.childNodes.length) name.childNodes[0].nodeValue = 'سناریو '+(idx+1)+' ';
      });
    }

    function addScenario(){
      const rows = document.querySelectorAll('.scenario-card');
      for(let i=0;i<rows.length;i++){
        if(rows[i].style.display==='none'){
          rows[i].style.display='block';
          const cb=rows[i].querySelector('input[name^="act_"]'); if(cb) cb.checked=true;
          const en=rows[i].querySelector('input[name^="en_"]'); if(en) en.checked=true;
          rows[i].classList.remove('scenario-disabled');
          const sw=rows[i].querySelector('.switch'); if(sw) sw.classList.add('on');
          const sh=rows[i].querySelector('input[name^="sh_"]'); if(sh) sh.value="";
          const eh=rows[i].querySelector('input[name^="eh_"]'); if(eh) eh.value="";
          updateScenarioOrder();
          return;
        }
      }
      showModal('حداکثر ظرفیت مجاز (۲۰ سناریو) را اضافه کرده‌اید.');
    }

    function removeScenario(index){
      const row = document.getElementById('row_'+index);
      if(row){
        row.style.display='none';
        const cb=row.querySelector('input[name^="act_"]'); if(cb) cb.checked=false;
        const en=row.querySelector('input[name^="en_"]'); if(en) en.checked=false;
        row.classList.remove('scenario-disabled','running','next');
        const sw=row.querySelector('.switch'); if(sw) sw.classList.remove('on');
        const sh=row.querySelector('input[name^="sh_"]'); if(sh) sh.value="";
        const eh=row.querySelector('input[name^="eh_"]'); if(eh) eh.value="";
        updateScenarioOrder();
      }
    }

    // پشتیبان‌گیری کاملاً محلی: فایل JSON در Downloads گوشی ذخیره می‌شود و روی ESP32 نوشته نمی‌شود.
    function scenarioRowsToData(){
      const list=[];
      for(let i=0;i<20;i++){
        const row=document.getElementById('row_'+i);
        if(!row || row.style.display==='none') continue;
        const sh=row.querySelector('input[name^="sh_"]').value, eh=row.querySelector('input[name^="eh_"]').value;
        if(!sh || !eh) continue;
        const a=sh.split(':'), b=eh.split(':');
        const en=row.querySelector('input[name^="en_"]'); const wd=row.querySelector('input[name^="wd_"]');
        list.push({sh:+a[0],sm:+a[1],eh:+b[0],em:+b[1],en:!en||en.checked,wd:wd?parseInt(wd.value,10):127});
      }
      return list;
    }
    function exportScenarios(){
      const backup={version:1,type:'esp32-cooler-scenarios',exportedAt:new Date().toISOString(),scenarios:scenarioRowsToData()};
      const blob=new Blob([JSON.stringify(backup,null,2)],{type:'application/json'});
      const url=URL.createObjectURL(blob), a=document.createElement('a');
      const now=new Date(); const stamp=now.getFullYear()+'-'+String(now.getMonth()+1).padStart(2,'0')+'-'+String(now.getDate()).padStart(2,'0');
      a.href=url; a.download='cooler-scenarios-'+stamp+'.json'; document.body.appendChild(a); a.click(); a.remove(); URL.revokeObjectURL(url);
      showModal('فایل پشتیبان سناریوها در گوشی شما دانلود شد.');
    }
    function importScenarios(event){
      const file=event.target.files && event.target.files[0]; if(!file) return;
      const reader=new FileReader();
      reader.onload=function(){
        try{
          const parsed=JSON.parse(reader.result); const data=Array.isArray(parsed)?parsed:parsed.scenarios;
          if(!Array.isArray(data) || data.length>20) throw new Error('format');
          for(const x of data){
            if(!x || !Number.isInteger(x.sh)||!Number.isInteger(x.sm)||!Number.isInteger(x.eh)||!Number.isInteger(x.em)||x.sh<0||x.sh>23||x.eh<0||x.eh>23||x.sm<0||x.sm>59||x.em<0||x.em>59||x.sh===x.eh&&x.sm===x.em) throw new Error('data');
          }
          // فقط فرم را پر می‌کند؛ ذخیره نهایی با دکمه اصلی و تأیید کاربر انجام می‌شود.
          for(let i=0;i<20;i++) removeScenario(i);
          data.forEach((x,i)=>{
            const row=document.getElementById('row_'+i); if(!row) return;
            row.style.display='block'; row.querySelector('input[name^="act_"]').checked=true;
            const en=row.querySelector('input[name^="en_"]'); en.checked=x.en!==false;
            const sw=row.querySelector('.switch'); sw.classList.toggle('on',en.checked); row.classList.toggle('scenario-disabled',!en.checked);
            row.querySelector('input[name^="sh_"]').value=String(x.sh).padStart(2,'0')+':'+String(x.sm).padStart(2,'0');
            row.querySelector('input[name^="eh_"]').value=String(x.eh).padStart(2,'0')+':'+String(x.em).padStart(2,'0');
            const wd=row.querySelector('input[name^="wd_"]'); const mask=Number.isInteger(x.wd)&&x.wd>=1&&x.wd<=127?x.wd:127; wd.value=mask;
            row.querySelectorAll('.day-btn').forEach(btn=>btn.classList.toggle('selected',(mask&(1<<parseInt(btn.dataset.day,10)))!==0));
          });
          updateScenarioOrder(); showModal('سناریوها از فایل خوانده شدند. برای اعمال روی برد، دکمه «ذخیره و پیاده‌سازی برنامه‌ها» را بزنید.');
        }catch(err){ showModal('فایل پشتیبان معتبر نیست یا با این برنامه سازگار نیست.'); }
        event.target.value='';
      }; reader.readAsText(file,'UTF-8');
    }

    function validateForm(event){
      document.querySelectorAll('.scenario-card').forEach(row=>row.classList.remove('error-conflict'));
      let activeScenarios=[];
      for(let i=0;i<20;i++){
        const row=document.getElementById('row_'+i);
        if(row && row.style.display!=='none'){
          const enInput=row.querySelector('input[name^="en_"]');
          const scenarioEnabled = !enInput || enInput.checked;
          const sh=row.querySelector('input[name^="sh_"]').value;
          const eh=row.querySelector('input[name^="eh_"]').value;
          const hasStart=(sh!=="");
          const hasEnd=(eh!=="");
          if((hasStart&&!hasEnd)||(!hasStart&&hasEnd)||(!hasStart&&!hasEnd)){
            row.classList.add('error-conflict');
            showModal('لطفاً هر دو کادر زمان روشن شدن و خاموش شدن را در تمام سناریوها به دقت پر کنید.');
            event.preventDefault(); return false;
          }
          if(hasStart&&hasEnd){
            let sp=sh.split(':'), ep=eh.split(':');
            if(sp.length===2&&ep.length===2){
              let startMin=parseInt(sp[0])*60+parseInt(sp[1]);
              let endMin=parseInt(ep[0])*60+parseInt(ep[1]);
              if(startMin===endMin){
                row.classList.add('error-conflict');
                showModal('در یکی از سناریوها زمان روشن و خاموش شدن یکسان است. این کار مجاز نیست.');
                event.preventDefault(); return false;
              }
              if(scenarioEnabled) activeScenarios.push({start:startMin,end:endMin,id:i});
            }
          }
        }
      }
      let timeMap=new Array(1440).fill(null);
      let conflictedIds=new Set();
      for(let s of activeScenarios){
        let st=s.start, en=s.end, mins=[];
        // مثل فریمور، زمان پایان جزو بازه نیست؛ بنابراین دو سناریوی پشت‌سرهم
        // مثل 10:00-11:00 و 11:00-12:00 نباید تداخل حساب شوند.
        if(st<en){ for(let m=st;m<en;m++) mins.push(m); }
        else { for(let m=st;m<1440;m++) mins.push(m); for(let m=0;m<en;m++) mins.push(m); }
        for(let m of mins){
          if(timeMap[m]!==null){ conflictedIds.add(s.id); conflictedIds.add(timeMap[m]); }
          else timeMap[m]=s.id;
        }
      }
      if(conflictedIds.size>0){
        conflictedIds.forEach(id=>{ const r=document.getElementById('row_'+id); if(r) r.classList.add('error-conflict'); });
        showModal('تداخل زمانی! بازه‌های سناریوها روی هم افتاده‌اند. لطفا زمان سناریوهای مشخص شده با کادر قرمز رنگ را اصلاح کنید.');
        event.preventDefault(); return false;
      }
      return true;
    }

    setTimeout(()=>{
      updateScenarioOrder();
      const list=document.getElementById('scenarios-list');
      if(list){
        list.addEventListener('change', updateScenarioOrder);
        list.addEventListener('input', function(e){ updateScenarioOrder(); let row=e.target.closest('.scenario-card'); if(row) row.classList.remove('error-conflict'); });
      }
    },100);

    let lastActivity=Date.now();
    let currentInterval=1000;
    let pingTimeoutId=null;
    let waitingWifiReboot=false, wifiWasDisconnected=false;
    let lastStaState=null; // آخرین وضعیت شناخته‌شده STA (0=غیرفعال،1=درحال اتصال،2=متصل،3=قطع،4=خاموش طبق چرخه) — برای تشخیص لحظه‌ی تغییر و نمایش نوتیفیکیشن
    function resetActivity(){
      lastActivity=Date.now();
      if(currentInterval!==1000){ currentInterval=1000; if(pingTimeoutId) clearTimeout(pingTimeoutId); fetchStatus(); }
    }
    ['click','touchstart','input','change','scroll'].forEach(evt=>document.addEventListener(evt,resetActivity,{passive:true}));

    // ذخیره تنظیمات فرستنده (AP) — این تغییر شبکه‌ی خود برد را عوض می‌کند، پس گوشی باید دوباره وصل شود
    // و برد ری‌استارت می‌شود. همان روال قبلی (مودال پیشرفت + انتظار اتصال مجدد) اینجا حفظ شده.
    function saveApForm(event){
      event.preventDefault();
      const form=event.target;
      const passInput=form.querySelector('input[name="pass"]');
      if(passInput && passInput.value.length<8){
        showModal('رمز شبکه (AP) خود برد باید حداقل ۸ کاراکتر باشد.');
        return false;
      }
      const modal=document.getElementById('progress-modal');
      const overlay=document.getElementById('custom-alert-overlay');
      const fill=document.getElementById('progress-bar-fill');
      const pct=document.getElementById('progress-percent');
      const title=document.getElementById('progress-title');
      const text=document.getElementById('progress-text');
      title.innerText='در حال ذخیره تنظیمات فرستنده...';
      text.innerText='تنظیمات ذخیره شد و برد در حال راه‌اندازی مجدد است. لطفاً منتظر بمانید؛ برنامه به‌صورت خودکار دوباره متصل می‌شود.';
      fill.style.width='100%'; pct.innerText='100%';
      overlay.style.display='block'; modal.style.display='block';
      waitingWifiReboot=true; wifiWasDisconnected=false;
      switchTab('home');
      const fd=new FormData(form);
      fetch('/save-ap',{method:'POST', body:fd})
        .then(r=>{ if(!r.ok) throw new Error('bad'); return r.text(); })
        .then(()=>{ /* برد در حال ری‌استارت است؛ منتظر اتصال دوباره می‌مانیم */ })
        .catch(()=>{ waitingWifiReboot=false; wifiWasDisconnected=false; if(modal) modal.style.display='none'; if(overlay) overlay.style.display='none'; showModal('خطا در ذخیره تنظیمات فرستنده! لطفاً دوباره تلاش کنید.'); });
      return false;
    }

    // ذخیره تنظیمات مودم اینترنت (STA) — برد ری‌استارت نمی‌شود و پنل هیچ‌وقت "گیر" نمی‌کند؛
    // فقط یک درخواست کوتاه ارسال می‌شود و بلافاصله وضعیت به‌روزرسانی می‌شود.
    function saveStaForm(event){
      event.preventDefault();
      const form=event.target;
      const passInput=form.querySelector('input[name="sta_pass"]');
      if(passInput && passInput.value.length>0 && passInput.value.length<8){
        showModal('رمز وای‌فای مودم اگر وارد شود باید حداقل ۸ کاراکتر باشد.');
        return false;
      }
      const onInput=form.querySelector('input[name="sta_on_minutes"]');
      const offInput=form.querySelector('input[name="sta_off_minutes"]');
      const onVal=onInput?Number(onInput.value):NaN;
      const offVal=offInput?Number(offInput.value):NaN;
      if(!Number.isInteger(onVal) || onVal<1 || onVal>1440 || !Number.isInteger(offVal) || offVal<0 || offVal>1440){
        showModal('زمان روشن بودن STA باید عدد صحیح بین ۱ تا ۱۴۴۰ و زمان خاموش بودن عدد صحیح بین ۰ تا ۱۴۴۰ دقیقه باشد.');
        return false;
      }
      const statusEl=document.getElementById('sta-status-inline');
      const cycleEl=document.getElementById('sta-cycle-status-inline');
      const internetModeEl=document.getElementById('internet-mode-inline');
      if(statusEl) statusEl.innerText='در حال ذخیره و اعمال اتصال مودم...';
      if(cycleEl) cycleEl.innerText='در حال ذخیره...';
      if(internetModeEl) internetModeEl.innerText='در حال ذخیره...';
      const fd=new FormData(form);
      const internetEnabled=document.getElementById('internet-enabled');
      fd.set('internet', (internetEnabled && internetEnabled.checked) ? '1' : '0');
      fetch('/save-sta',{method:'POST', body:fd})
        .then(r=>{ if(!r.ok) throw new Error('bad'); return r.text(); })
        .then(()=>{ showModal('تنظیمات مودم / اینترنت ذخیره شد. چرخه اتصال STA با مقادیر جدید از نو شروع شد.'); resetActivity(); fetchStatus(); })
        .catch(()=>{ if(statusEl) statusEl.innerText='خطا'; if(cycleEl) cycleEl.innerText='خطا'; if(internetModeEl) internetModeEl.innerText='خطا'; showModal('خطا در ذخیره تنظیمات مودم / اینترنت! لطفاً دوباره تلاش کنید.'); });
      return false;
    }

    function saveProtectionForm(event){
      event.preventDefault(); const form=event.target; const input=form.querySelector('input[name="min_off"]');
      const value=input ? Number(input.value) : NaN;
      if(!Number.isInteger(value) || value<0 || value>1440){ showModal('زمان محافظت باید یک عدد صحیح بین ۰ تا ۱۴۴۰ دقیقه باشد.'); return false; }
      fetch('/save-protection',{method:'POST',body:new FormData(form)})
        .then(r=>{if(!r.ok) throw new Error('bad'); return r.text();})
        .then(()=>{showModal('تنظیمات محافظت کمپرسور ذخیره شد.'); fetchStatus();})
        .catch(()=>showModal('خطا در ذخیره تنظیمات محافظت!'));
      return false;
    }

    function saveApCycleForm(event){
      event.preventDefault();
      const form=event.target;
      const onInput=form.querySelector('input[name="on_minutes"]');
      const offInput=form.querySelector('input[name="off_minutes"]');
      const onVal=onInput?Number(onInput.value):NaN;
      const offVal=offInput?Number(offInput.value):NaN;
      if(!Number.isInteger(onVal) || onVal<1 || onVal>1440 || !Number.isInteger(offVal) || offVal<1 || offVal>1440){
        showModal('مدت روشن و خاموش بودن هرکدام باید عددی صحیح بین ۱ تا ۱۴۴۰ دقیقه باشد.');
        return false;
      }
      const cycleEnabled=document.getElementById('ap-cycle-enabled');
      const fd=new FormData(form);
      // چون هنگام غیرفعال بودن، مقدار چک‌باکس در فرم واقعی ارسال نمی‌شود، صریحاً همان مقدار جاری اضافه می‌شود.
      fd.set('cycle_enabled', (cycleEnabled && cycleEnabled.checked) ? '1' : '0');
      fetch('/save-ap-cycle',{method:'POST', body:fd})
        .then(r=>{ if(!r.ok) throw new Error('bad'); return r.text(); })
        .then(()=>{ showModal('تنظیمات چرخه AP ذخیره شد.'); fetchStatus(); })
        .catch(()=>showModal('خطا در ذخیره تنظیمات چرخه AP! لطفاً دوباره تلاش کنید.'));
      return false;
    }

    function saveScenariosForm(event){
      event.preventDefault();
      if(!validateForm(event)) return false;
      const modal=document.getElementById('progress-modal');
      const overlay=document.getElementById('custom-alert-overlay');
      const fill=document.getElementById('progress-bar-fill');
      const pct=document.getElementById('progress-percent');
      const title=document.getElementById('progress-title');
      const text=document.getElementById('progress-text');
      title.innerText="در حال ذخیره سناریوها...";
      text.innerText="لطفاً منتظر بمانید، اطلاعات به طور امن روی حافظه بورد نوشته می‌شود.";
      fill.style.width='0%'; pct.innerText='0%';
      overlay.style.display='block'; modal.style.display='block';
      let val=0;
      let intId=setInterval(()=>{ if(val<90){ val+=Math.floor(Math.random()*8)+4; if(val>90) val=90; fill.style.width=val+'%'; pct.innerText=val+'%'; } },120);
      let valid=[];
      for(let i=0;i<20;i++){
        const row=document.getElementById('row_'+i);
        if(row && row.style.display!=='none'){
          const enInput=row.querySelector('input[name^="en_"]');
          const enabled=!enInput||enInput.checked;
          const shVal=row.querySelector('input[name^="sh_"]').value;
          const ehVal=row.querySelector('input[name^="eh_"]').value;
          if(shVal&&ehVal){
            let sp=shVal.split(':'), ep=ehVal.split(':');
            if(sp.length===2&&ep.length===2){
              let sh=parseInt(sp[0],10), sm=parseInt(sp[1],10), eh=parseInt(ep[0],10), em=parseInt(ep[1],10);
              if(sh!==eh||sm!==em){ const wdInput=row.querySelector('input[name^="wd_"]'); let wd=wdInput ? parseInt(wdInput.value,10) : 127; if(isNaN(wd)||wd<1||wd>127) wd=127; valid.push({sh:sh,sm:sm,eh:eh,em:em,en:enabled,wd:wd}); }
            }
          }
        }
      }
      valid.sort((a,b)=>(a.sh*60+a.sm)-(b.sh*60+b.sm));
      fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(valid)})
        .then(r=>{ clearInterval(intId); fill.style.width='100%'; pct.innerText='100%'; title.innerText="ذخیره‌سازی موفقیت‌آمیز بود ✅"; text.innerText="برنامه‌ریزی جدید سناریوها بدون قطع ارتباط روی بورد ثبت شد."; setTimeout(()=>{ modal.style.display='none'; overlay.style.display='none'; resetActivity(); fetchStatus(); updateScenarioOrder(); },1600); })
        .catch(err=>{ clearInterval(intId); modal.style.display='none'; overlay.style.display='none'; showModal('خطا در برقراری ارتباط با برد!'); resetActivity(); });
      return false;
    }

    function highlightScenarios(boardTimeStr, currentWeekday){
      if(!boardTimeStr || boardTimeStr==='--:--:--') return;
      let parts=boardTimeStr.split(':');
      if(parts.length<2) return;
      let cur=parseInt(parts[0])*60+parseInt(parts[1]);
      let cards=document.querySelectorAll('.scenario-card');
      cards.forEach(c=>{
        c.classList.remove('running','next');
        const nm=c.querySelector('.scenario-name');
        if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); }
      });
      let data=[];
      cards.forEach(c=>{
        if(c.style.display!=='none' && !c.classList.contains('scenario-disabled')){
          let sh=c.querySelector('input[name^="sh_"]'), eh=c.querySelector('input[name^="eh_"]');
          if(sh&&eh&&sh.value&&eh.value&&!c.classList.contains('error-conflict')){
            let sp=sh.value.split(':'), ep=eh.value.split(':');
            if(sp.length===2&&ep.length===2){ let s=parseInt(sp[0])*60+parseInt(sp[1]); let e=parseInt(ep[0])*60+parseInt(ep[1]); if(s!==e){ const wdInput=c.querySelector('input[name^="wd_"]'); let wd=wdInput ? parseInt(wdInput.value,10) : 127; if(isNaN(wd)) wd=127; data.push({card:c,s:s,e:e,wd:wd}); } }
          }
        }
      });
      let anyRunning=false, next=null, minDiff=Infinity;
      data.forEach(d=>{
        let running = d.s<d.e ? (cur>=d.s&&cur<d.e) : (cur>=d.s||cur<d.e);
        let scenarioDay=(d.s>d.e && cur<d.e) ? ((currentWeekday+6)%7) : currentWeekday;
        running = running && ((d.wd & (1<<scenarioDay))!==0);
        if(running){ d.card.classList.add('running'); anyRunning=true; const nm=d.card.querySelector('.scenario-name'); const b=document.createElement('span'); b.className='badge-live'; b.textContent='در حال اجرا'; nm.appendChild(b); }
        else { let diff=d.s-cur; if(diff<0) diff+=1440; if(diff<minDiff){ minDiff=diff; next=d; } }
      });
      if(!anyRunning && next){ next.card.classList.add('next'); const nm=next.card.querySelector('.scenario-name'); const b=document.createElement('span'); b.className='badge-next'; b.textContent='بعدی'; nm.appendChild(b); }
    }

    // نوتیفیکیشن کوتاه و خودکار-محو (Toast) — برای اطلاع‌رسانی فوری قطع/وصل شدن مودم، بدون نیاز به تایید کاربر
    // (بر خلاف showModal که یک پنجره‌ی مسدودکننده است و باید کاربر آن را ببندد)
    function showToast(msg, kind){
      const wrap = document.getElementById('toast-wrap');
      if(!wrap) return;
      const el = document.createElement('div');
      el.className = 'toast toast-' + kind;
      el.innerText = msg;
      wrap.appendChild(el);
      requestAnimationFrame(()=>el.classList.add('show'));
      setTimeout(()=>{
        el.classList.remove('show');
        setTimeout(()=>el.remove(), 300);
      }, 3800);
    }

    // تبدیل تاریخ میلادی داخلی ESP32 به هجری شمسی؛ منطق زمان‌بندی همچنان با تاریخ دقیق میلادی کار می‌کند.
    function gregorianToJalali(gy, gm, gd){
      const gdm=[0,31,59,90,120,151,181,212,243,273,304,334];
      let gy2=(gm>2)?gy+1:gy;
      let days=355666+365*gy+Math.floor((gy2+3)/4)-Math.floor((gy2+99)/100)+Math.floor((gy2+399)/400)+gd+gdm[gm-1];
      let jy=-1595+33*Math.floor(days/12053); days%=12053;
      jy+=4*Math.floor(days/1461); days%=1461;
      if(days>365){ jy+=Math.floor((days-1)/365); days=(days-1)%365; }
      let jm, jd;
      if(days<186){ jm=1+Math.floor(days/31); jd=1+(days%31); }
      else { jm=7+Math.floor((days-186)/30); jd=1+((days-186)%30); }
      return [jy,jm,jd];
    }
    function formatJalaliDate(gy,gm,gd){
      const j=gregorianToJalali(gy,gm,gd);
      const fa=new Intl.NumberFormat('fa-IR',{useGrouping:false});
      return fa.format(j[0])+'/'+fa.format(j[1]).padStart(2,'۰')+'/'+fa.format(j[2]).padStart(2,'۰');
    }

    // تبدیل ثانیه به متن قابل‌فهم فارسی (روز/ساعت/دقیقه) برای نمایش مجموع مدت‌کارکرد کمپرسور
    function formatDuration(totalSeconds){
      if(totalSeconds==null || totalSeconds<0) return '--';
      const days = Math.floor(totalSeconds / 86400);
      const hours = Math.floor((totalSeconds % 86400) / 3600);
      const minutes = Math.floor((totalSeconds % 3600) / 60);
      if(days > 0) return days + ' روز و ' + hours + ' ساعت';
      if(hours > 0) return hours + ' ساعت و ' + minutes + ' دقیقه';
      if(minutes > 0) return minutes + ' دقیقه';
      return totalSeconds + ' ثانیه';
    }

    function pad2(n){ return String(n).padStart(2,'0'); }

    // نمایش آخرین دریافت موفق ساعت از اینترنت به‌صورت تاریخ/ساعت مطلق، نه «چند دقیقه پیش».
    function formatNtpSuccessStamp(data){
      if(!data || data.ntpLastValid!==1) return 'هنوز دریافت نشده';
      const j=gregorianToJalali(data.ntpYear, data.ntpMonth, data.ntpDay);
      return j[0]+'/'+pad2(j[1])+'/'+pad2(j[2])+' - '+pad2(data.ntpHour)+':'+pad2(data.ntpMinute)+':'+pad2(data.ntpSecond);
    }

    function fetchStatus(){
      if(document.hidden){ if(pingTimeoutId) clearTimeout(pingTimeoutId); pingTimeoutId=setTimeout(fetchStatus,currentInterval); return; }
      let reqStart=Date.now();
      const controller=new AbortController();
      const timeoutId=setTimeout(()=>controller.abort(),2500);
      fetch('/status?t='+reqStart,{signal:controller.signal})
        .then(r=>{ clearTimeout(timeoutId); return r.json(); })
        .then(data=>{
          let ping=Date.now()-reqStart;
          let badge=document.getElementById('connection-status');
          badge.className='conn-badge connected';
          badge.innerHTML='<span class="dot"></span> متصل · '+ping+'ms';
          document.getElementById('board-time').innerText=data.time;
          const dayNames=['شنبه','یکشنبه','دوشنبه','سه‌شنبه','چهارشنبه','پنجشنبه','جمعه'];
          const dateEl=document.getElementById('board-date');
          if(dateEl && data.year!=null) dateEl.innerText='امروز: '+dayNames[data.weekday]+' — '+formatJalaliDate(data.year,data.month,data.day)+'';

          if(waitingWifiReboot && wifiWasDisconnected){
            waitingWifiReboot=false; wifiWasDisconnected=false;
            const _m=document.getElementById('progress-modal'); const _o=document.getElementById('custom-alert-overlay');
            if(_m) _m.style.display='none'; if(_o) _o.style.display='none';
          }

          let hero=document.getElementById('cooler-display-status');
          let statusText=document.getElementById('cooler-status-text');
          let statusSub=document.getElementById('cooler-status-sub');
          let scenariosTabBtn=document.getElementById('tab-btn-scenarios');
          let syncWarning=document.getElementById('sync-warning');
          let manualBtn=document.getElementById('manual-btn');
          let internetWifiStatus=document.getElementById('internet-wifi-status');

          // --- وضعیت STA: نارنجی=درحال اتصال، سبز=وصل، قرمز=قطع ناخواسته، حالت چرخهٔ خاموش=متن زمان‌بندی ---
          // staState: 0=غیرفعال/تنظیم‌نشده، 1=درحال تلاش برای اتصال، 2=متصل، 3=قبلاً وصل بوده و الان قطع، 4=خاموش طبق چرخه
          const staState = data.staState;
          let staText, staClass;
          if(data.internetEnabled!==1){ staText='غیرفعال چون اینترنت خاموش است'; staClass='status-connecting'; }
          else if(data.staConfigured!==1){ staText='تنظیم نشده'; staClass='status-connecting'; }
          else if(staState===4){ staText='طبق زمان‌بندی موقتاً قطع است'; staClass='status-connecting'; }
          else if(staState===2){ staText='متصل - '+data.staIp; staClass='status-connected'; }
          else if(staState===3){ staText='⚠️ قطع شده - در حال تلاش مجدد...'; staClass='status-disconnected'; }
          else if(staState===1){ staText='در حال اتصال...'; staClass='status-connecting'; }
          else { staText='در حال بررسی...'; staClass='status-connecting'; }

          let staCycleText, staCycleClass;
          if(data.internetEnabled!==1){
            staCycleText='غیرفعال';
            staCycleClass='status-connecting';
          } else if(data.staConfigured!==1){
            staCycleText='تا قبل از وارد کردن SSID مودم فعال نمی‌شود';
            staCycleClass='status-connecting';
          } else if(data.staOffMinutes===0){
            staCycleText='دائم روشن (زمان خاموشی = ۰)';
            staCycleClass='status-connected';
          } else if(data.staPhaseOn===1){
            staCycleText='روشن — '+Math.ceil(data.staRemaining/60)+' دقیقه تا قطع دوره‌ای';
            staCycleClass='status-connected';
          } else {
            staCycleText='خاموش — '+Math.ceil(data.staRemaining/60)+' دقیقه تا وصل مجدد';
            staCycleClass='status-connecting';
          }

          let internetModeText, internetModeClass;
          if(data.internetEnabled===1){
            if(data.staConfigured!==1){ internetModeText='فعال است، اما مودم تنظیم نشده'; internetModeClass='status-connecting'; }
            else if(data.staOffMinutes===0){ internetModeText='فعال — اتصال مودم دائماً روشن است'; internetModeClass='status-connected'; }
            else { internetModeText='فعال — طبق زمان‌بندی STA'; internetModeClass='status-connected'; }
          } else {
            internetModeText='غیرفعال';
            internetModeClass='status-connecting';
          }

          if(internetWifiStatus){ internetWifiStatus.textContent=staText; internetWifiStatus.className=staClass; }
          const staCycleHome=document.getElementById('sta-cycle-status');
          if(staCycleHome){ staCycleHome.textContent=staCycleText; staCycleHome.className=staCycleClass; }
          const internetModeHome=document.getElementById('internet-mode-status');
          if(internetModeHome){ internetModeHome.textContent=internetModeText; internetModeHome.className=internetModeClass; }
          const staInline=document.getElementById('sta-status-inline');
          if(staInline){ staInline.textContent=staText; staInline.className=staClass; }
          const staCycleInline=document.getElementById('sta-cycle-status-inline');
          if(staCycleInline){ staCycleInline.textContent=staCycleText; staCycleInline.className=staCycleClass; }
          const internetModeInline=document.getElementById('internet-mode-inline');
          if(internetModeInline){ internetModeInline.textContent=internetModeText; internetModeInline.className=internetModeClass; }

          // بازخورد فوری (Toast) دقیقاً روی لحظه‌ی تغییر واقعی وضعیت — نه هر بار که پنل آپدیت می‌شود
          if(lastStaState!==null && lastStaState!==staState && data.internetEnabled===1 && data.staConfigured===1){
            if(staState===3){ showToast('⚠️ اتصال STA به مودم اینترنت قطع شد', 'bad'); }
            else if(staState===2 && lastStaState===3){ showToast('✅ اتصال STA به مودم دوباره برقرار شد', 'good'); }
            else if(staState===2 && lastStaState===1){ showToast('✅ اتصال STA به مودم برقرار شد', 'good'); }
          }
          lastStaState=staState;

          const apCycleEl=document.getElementById('ap-cycle-status');
          if(apCycleEl){
            if(!data.apCycleEnabled){ apCycleEl.textContent='غیرفعال — فرستنده همیشه روشن است'; apCycleEl.style.color='var(--text-dim)'; }
            else if(data.apOn && data.apClientConnected){ apCycleEl.textContent='روشن — دستگاهی متصل است، چرخه متوقف مانده'; apCycleEl.style.color='var(--scenario-live)'; }
            else if(data.apOn){ apCycleEl.textContent='روشن — '+Math.ceil(data.apRemaining/60)+' دقیقه تا خاموش‌شدن'; apCycleEl.style.color='var(--scenario-live)'; }
            else { apCycleEl.textContent='در حال خاموش بودن طبق چرخه'; apCycleEl.style.color='var(--warn)'; }
          }

          const protectionEl=document.getElementById('protection-status');
          if(protectionEl){
            if(data.protectionRemaining>0){ protectionEl.textContent='فعال — '+Math.ceil(data.protectionRemaining/60)+' دقیقه تا اجازه روشن‌شدن'; protectionEl.style.color='var(--warn)'; }
            else if(data.protectionMinutes>0){ protectionEl.textContent='فعال — فاصله تنظیم‌شده: '+data.protectionMinutes+' دقیقه'; protectionEl.style.color='var(--scenario-live)'; }
            else { protectionEl.textContent='غیرفعال'; protectionEl.style.color='var(--text-dim)'; }
          }

          const ntpStampText=formatNtpSuccessStamp(data);
          const ntpColor = data.ntpLastValid===1 ? 'var(--scenario-live)' : 'var(--warn)';
          const ntpHome=document.getElementById('ntp-last-update');
          if(ntpHome){ ntpHome.innerText=ntpStampText; ntpHome.style.color=ntpColor; }
          const ntpInline=document.getElementById('ntp-last-update-inline');
          if(ntpInline){ ntpInline.innerText=ntpStampText; ntpInline.style.color=ntpColor; }

          // --- آمار سوییچ و مدت‌کارکرد رله (سلامت/عمر کمپرسور) ---
          const switchCountEl=document.getElementById('relay-switch-count');
          if(switchCountEl && data.switchCount!=null) switchCountEl.innerText=data.switchCount+' بار';
          const onDurationEl=document.getElementById('relay-on-duration');
          if(onDurationEl && data.onSeconds!=null) onDurationEl.innerText=formatDuration(data.onSeconds);

          if(data.sync===1){ syncWarning.innerText='ساعت همگام‌سازی شده و سناریوها فعال هستند.'; syncWarning.style.color='var(--on)'; }
          else { syncWarning.innerText='⚠️ ساعت برد همگام نیست! سناریوها تا زمان همگام‌سازی اجرا نخواهند شد.'; syncWarning.style.color='var(--off)'; }

          if(data.relay===1){ hero.className='panel hero on'; statusText.innerText='کولر روشن است'; }
          else { hero.className='panel hero off'; statusText.innerText='کولر خاموش است'; }

          if(statusSub) statusSub.innerText = data.protectionRemaining>0 ? 'محافظت کمپرسور: '+Math.ceil(data.protectionRemaining/60)+' دقیقه تا روشن‌شدن' : (data.override===1 ? 'حالت: دستی (روشن)' : 'حالت: خودکار (سناریو)');

          if(manualBtn){
            if(data.override===1 && manualBtn.classList.contains('isoff')){ manualBtn.classList.remove('isoff'); manualBtn.innerText='خاموش کردن دستی کولر'; }
            else if(data.override===0 && !manualBtn.classList.contains('isoff')){ manualBtn.classList.add('isoff'); manualBtn.innerText='روشن کردن دستی کولر'; }
          }

          const manualNote=document.getElementById('manual-disabled-note');
          if(manualNote) manualNote.style.display = (data.override===1) ? 'block' : 'none';

          if(data.sync===1 && data.override===0) highlightScenarios(data.time, data.weekday);
          else document.querySelectorAll('.scenario-card').forEach(r=>{ r.classList.remove('running','next'); const nm=r.querySelector('.scenario-name'); if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); } });

          queueNextPing();
        })
        .catch(err=>{
          let badge=document.getElementById('connection-status');
          badge.className='conn-badge disconnected';
          if(waitingWifiReboot) wifiWasDisconnected=true;
          badge.innerHTML='<span class="dot"></span> قطع ارتباط!';
          let hero=document.getElementById('cooler-display-status');
          let statusText=document.getElementById('cooler-status-text');
          if(hero) hero.className='panel hero disconnected';
          if(statusText) statusText.innerText='ارتباط با برد قطع شده است';
          document.querySelectorAll('.scenario-card').forEach(r=>{ r.classList.remove('running','next'); const nm=r.querySelector('.scenario-name'); if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); } });
          queueNextPing();
        });
    }

    function queueNextPing(){
      if(pingTimeoutId) clearTimeout(pingTimeoutId);
      let since=Date.now()-lastActivity;
      currentInterval = since>30000 ? 3000 : 1000;
      pingTimeoutId=setTimeout(fetchStatus,currentInterval);
    }

    fetchStatus();
    document.addEventListener('visibilitychange',()=>{ if(!document.hidden) resetActivity(); });
  </script>
</body>
</html>
)rawliteral";

#endif
