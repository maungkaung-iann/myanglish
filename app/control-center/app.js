const pages=[...document.querySelectorAll('.page')],navs=[...document.querySelectorAll('.nav')];
function go(id){pages.forEach(p=>p.classList.toggle('active',p.id===id));navs.forEach(n=>n.classList.toggle('active',n.dataset.page===id));}
navs.forEach(n=>n.onclick=()=>go(n.dataset.page));document.querySelectorAll('[data-go]').forEach(b=>b.onclick=()=>go(b.dataset.go));
const features=[
 {cat:'Typing',type:'typing',action:'Myanglish Typing',desc:'အင်္ဂလိပ်လက်ကွက်နဲ့ မြန်မာစာကို အသံထွက်အတိုင်း ရိုက်ပါ။',key:'Type'},
 {cat:'Typing',type:'typing',action:'Candidate ရွေးရန်',desc:'ပထမ Space မှာ ပထမ candidate ကိုရွေးပြီး ဆက်နှိပ်ရင် candidate တွေကို လှည့်ရွေးနိုင်ပါတယ်။',key:'Space'},
 {cat:'Typing',type:'typing',action:'စာလုံးအတည်ပြုရန်',desc:'ရွေးထားတဲ့ candidate ကို commit လုပ်ပါတယ်။',key:'Enter'},
 {cat:'Typing',type:'typing',action:'Fast Typing / Auto-Commit',desc:'နောက်စာလုံးကို ဆက်ရိုက်တဲ့အခါ လက်ရှိစကားလုံးကို အလိုအလျောက် commit လုပ်နိုင်ပါတယ်။',key:'Next key'},
 {cat:'Keyboard',type:'keyboard',action:'Easy Myanmar Stacking',desc:'မြန်မာစာလုံးဆင့် ရိုက်ရန် Shift ကို အသုံးပြုနိုင်ပါတယ်။',key:'Hold Shift'},
 {cat:'Keyboard',type:'keyboard',action:'English Mode',desc:'Myanglish conversion မလုပ်ဘဲ native English keyboard ကို သုံးနိုင်ပါတယ်။',key:'CapsLock'},
 {cat:'Keyboard',type:'keyboard',action:'Myanmar Punctuation',desc:'မြန်မာ ပုဒ်ဖြတ်အမှတ်တွေကို keyboard ကနေ တိုက်ရိုက်ရိုက်နိုင်ပါတယ်။',key:',  /  .'},
 {cat:'Keyboard',type:'keyboard',action:'Commit + Space',desc:'လက်ရှိစာလုံးကို commit လုပ်ပြီး space တစ်ခု ထည့်ပါတယ်။',key:'Tab'},
 {cat:'Typing',type:'typing',action:'Cancel Candidate',desc:'လက်ရှိ preview/candidate ကို ပယ်ဖျက်နိုင်ပါတယ်။',key:'Esc'},
 {cat:'Typing',type:'typing',action:'Back / Cancel',desc:'လက်ရှိ input ကို နောက်ပြန်ဖျက် သို့မဟုတ် candidate ကို ပယ်ဖျက်နိုင်ပါတယ်။',key:'Backspace'},
 {cat:'Dictionary',type:'dictionary',action:'Learns Your Choices',desc:'သင်ရွေးချယ်တဲ့ စကားလုံးတွေကို မှတ်သားပြီး အသုံးပြုမှုကို ပိုလွယ်ကူစေပါတယ်။',key:'Automatic'},
 {cat:'Dictionary',type:'dictionary',action:'Add Your Own Words',desc:'ကိုယ်ပိုင် Myanglish → Myanmar စကားလုံးတွေကို Personal Dictionary ထဲ ထည့်နိုင်ပါတယ်။',key:'Add Words'}
];
const featureList=document.getElementById('featureList');
const featureSearch=document.getElementById('featureSearch');
const featureFilter=document.getElementById('featureFilter');
function renderFeatures(){
 const q=(featureSearch?.value||'').trim().toLowerCase(), f=featureFilter?.value||'all';
 const rows=features.filter(x=>(f==='all'||x.type===f)&&(!q||(x.cat+' '+x.action+' '+x.desc+' '+x.key).toLowerCase().includes(q)));
 featureList.innerHTML=rows.map(x=>`<div class="feature-row"><span class="feature-cat"><i class="drag">⠿</i>${x.cat}</span><span class="feature-action"><b>${x.action}</b><small>${x.desc}</small></span><span><kbd>${x.key}</kbd></span></div>`).join('') || '<div class="feature-empty">Feature မတွေ့ပါ</div>';
}
featureSearch?.addEventListener('input',renderFeatures); featureFilter?.addEventListener('change',renderFeatures); renderFeatures();
let priority=1;document.querySelectorAll('.prio').forEach(b=>b.onclick=()=>{priority=+b.dataset.p;document.querySelectorAll('.prio').forEach(x=>x.classList.toggle('active',x===b))});
async function loadWords(){try{const r=await fetch('/api/words');const a=await r.json();document.getElementById('wordsList').innerHTML=a.map((w,i)=>`<div class="word"><b>${esc(w.raw)}</b><span>${esc(w.burmese)}</span><small>${w.priority||'1st'}</small><button onclick="delWord(${i})">Delete</button></div>`).join('')}catch{}}
function esc(s){return String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]))}
document.getElementById('add').onclick=async()=>{const raw=document.getElementById('raw').value.trim(),burmese=document.getElementById('burmese').value.trim();if(!raw||!burmese)return;const r=await fetch('/api/add',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({raw,burmese,priority})});if(r.ok){document.getElementById('raw').value='';document.getElementById('burmese').value='';document.getElementById('toast').textContent='✓ ထည့်ပြီးပါပြီ';setTimeout(()=>toast.textContent='',1800);loadWords()}};
async function delWord(i){await fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})});loadWords()}loadWords();
