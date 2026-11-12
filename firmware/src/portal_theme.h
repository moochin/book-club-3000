// The Wi-Fi setup page's Book-Club 3000 look.
//
// WiFiManager serves the page from the unit; this block is added to the
// <head> of every page after the library's own style, so it overrides it.
// Phones have no internet while joined to the setup network, so everything
// is inline: no web fonts, no external images or scripts.
//
// Tokens replaced at run time (net.cpp, portalHead()):
//   %A%  accent        %D%  dim accent      %F%  faint accent
//   %BG% background    %G%  glow (rgba)     %IMG% mascot PNG (base64)
//   %AP% the setup network's name
//
// The script swaps the library's English labels for the club's wording and
// wires the dropdowns to WiFiManager's hidden text fields.
// sim/portal_preview.py renders this file's text, so keep it a single raw
// string literal.
#pragma once

static const char kPortalHead[] = R"PORTAL(
<style>
:root{--a:%A%;--d:%D%;--f:%F%;--bg:%BG%;--g:%G%}
html{background:var(--bg)}
body,body.invert{background:var(--bg) repeating-linear-gradient(0deg,rgba(0,0,0,.28) 0 1px,transparent 1px 3px);
 color:var(--a);font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono","Courier New",monospace;
 text-shadow:0 0 5px var(--g);margin:0;padding:12px 0 28px}
body.invert a,body.invert h1,a{background:none;color:var(--a)}
.wrap{max-width:460px;width:100%;padding:0 14px;box-sizing:border-box}
h1{font-size:1.55rem;letter-spacing:.14em;text-transform:uppercase;margin:6px 0 0;text-align:center;text-shadow:0 0 10px var(--g)}
h1:after{content:'_';animation:bcb 1s steps(1) infinite}
@keyframes bcb{50%{opacity:0}}
h3{font-size:.8rem;font-weight:normal;letter-spacing:.2em;text-transform:uppercase;color:var(--d);text-align:center;margin:4px 0 14px}
.bc-logo{display:block;margin:4px auto 0;width:96px;height:144px;image-rendering:pixelated;image-rendering:crisp-edges;
 filter:drop-shadow(0 0 6px var(--g))}
button,input[type=button],input[type=submit]{background:transparent;color:var(--a);border:2px solid var(--a);border-radius:0;
 font-family:inherit;font-size:1rem;letter-spacing:.14em;text-transform:uppercase;line-height:2.6rem;
 box-shadow:0 0 8px var(--g),inset 0 0 8px var(--g);text-shadow:0 0 5px var(--g)}
button:active,button:hover{background:var(--a);color:var(--bg);opacity:1 !important}
button.D{border-color:#ff5030;color:#ff5030;background:transparent}
input,select{background:#000;color:var(--a);border:1px solid var(--d);border-radius:0;font-family:inherit;font-size:1rem;padding:8px}
input:focus,select:focus{outline:none;border-color:var(--a);box-shadow:0 0 8px var(--g)}
input::placeholder{color:var(--d);opacity:1}
input[type=checkbox]{accent-color:var(--a)}
label{color:var(--d);font-size:.78rem;letter-spacing:.12em;text-transform:uppercase}
hr{border:0;border-top:1px dashed var(--f);margin:14px 0}
.msg,body.invert .msg{background:#000;color:var(--a);border:1px solid var(--d);border-left:6px solid var(--a);border-radius:0}
.msg.D{border-left-color:#ff5030}
body.invert .q[role=img]{filter:invert(1) sepia(1) saturate(4) hue-rotate(%HUE%deg)}
.q{color:var(--d)}
dt{color:var(--a)}dd{color:var(--d)}
.bc-intro{border:1px solid var(--d);padding:10px 12px;margin:0 0 16px;line-height:1.45;font-size:.92rem}
.bc-intro b{color:var(--a)}
.bc-sec{margin:22px 0 4px;padding-bottom:3px;border-bottom:1px solid var(--d);letter-spacing:.22em;font-size:.95rem}
.bc-sec:before{content:'> '}
.bc-hint{color:var(--d);font-size:.75rem;line-height:1.35;margin:2px 0 4px}
.wrap>br,form br{display:none}
form button[type=submit]{margin-top:26px}
form label{display:block;margin-top:12px}
form input[type=checkbox]+label{display:inline;margin:0 0 0 6px}
.wrap>form{margin:10px 0}
.bc-top{display:flex;align-items:center;gap:10px;margin:0 0 12px;padding-bottom:8px;border-bottom:1px solid var(--d);
 letter-spacing:.14em;font-size:1.05rem;text-shadow:0 0 8px var(--g)}
.bc-top img{width:32px;height:48px;image-rendering:pixelated;image-rendering:crisp-edges}
details{margin-top:22px}summary{cursor:pointer;letter-spacing:.22em;color:var(--d)}
details[open] summary{color:var(--a)}
</style>
<script>
document.addEventListener('DOMContentLoaded',function(){
 var img='data:image/png;base64,%IMG%',h=document.querySelector('h1'),w=document.querySelector('.wrap');
 if(h){var i=new Image();i.className='bc-logo';i.alt='';i.src=img;h.parentNode.insertBefore(i,h);}
 else if(w){var t=document.createElement('div');t.className='bc-top';var s=new Image();s.alt='';s.src=img;
  t.appendChild(s);t.appendChild(document.createTextNode('BOOK-CLUB 3000'));w.insertBefore(t,w.firstChild);}
 var words={'Configure WiFi':'Connect this terminal','Configure WiFi (No scan)':'Connect (no scan)',
  'Info':'Terminal info','Exit':'Exit setup','Save':'Save & connect','Refresh':'Scan again','Back':'Back',
  'SSID':'Network','Password':'Network password','Show Password':'Show password','Erase':'Erase Wi-Fi',
  'Restart':'Restart terminal','Close':'Close'};
 document.querySelectorAll('button,label').forEach(function(e){var t=e.textContent.trim();if(words[t])e.textContent=words[t];});
 document.querySelectorAll('.msg').forEach(function(m){
  if(m.textContent.trim()==='No AP set')m.textContent='No home network saved yet.';
  if(m.textContent.indexOf('Saving Credentials')>=0)
   m.innerHTML='<b>Saved.</b><br>The terminal is joining your network now. If it can\'t, it will open this page again: rejoin <b>%AP%</b> and check the password.';});
 document.querySelectorAll('select[data-for]').forEach(function(s){
  var f=document.getElementById(s.getAttribute('data-for'));if(!f)return;
  if(f.value)s.value=f.value;f.value=s.value;
  s.addEventListener('change',function(){f.value=s.value;});});
});
</script>
)PORTAL";

// The opening text on the setup page's front screen.
static const char kPortalIntro[] =
    "<div class='bc-intro'><b>WELCOME, OPERATIVE.</b><br>This terminal counts the days since the "
    "club last met. Connect it to your home Wi-Fi and tell it who you are.</div>";

// Section headings and hints between the settings fields.
static const char kPortalSecOperative[] = "<div class='bc-sec'>OPERATIVE</div>";
static const char kPortalStatsHint[] =
    "<div class='bc-hint'>Owner card stats, seven numbers from 1 to 10: gaming skill, movie trivia, beer "
    "capacity, snack supply, banter, punctuality, staying power.</div>";
static const char kPortalSecTerminal[] =
    "<div class='bc-sec'>TERMINAL</div>"
    "<label for='bc-theme'>Screen colour</label>"
    "<select id='bc-theme' data-for='theme'><option value='green'>Phosphor green</option>"
    "<option value='amber'>Amber</option></select>"
    "<label for='bc-rot'>Screen rotation</label>"
    "<select id='bc-rot' data-for='rot'><option value='0'>0</option><option value='1'>1</option>"
    "<option value='2'>2</option><option value='3'>3 (in the case)</option></select>"
    "<div class='bc-hint'>Pick the one that reads the right way up in the case.</div>";
static const char kPortalSecClub[] = "<div class='bc-sec'>CLUB LINK</div>";
static const char kPortalAdvancedOpen[] = "<details><summary>ADVANCED</summary>";
static const char kPortalAdvancedClose[] = "</details>";
