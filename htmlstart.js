<html>
<head>
<title>RIS Worklist Manager</title>
<style>
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th, td { border: 1px solid #dddddd; padding: 12px; }
th { background: #009879; color: #fff; text-align: left; }
tr { border-bottom: 1px solid #ddd; }
tr:nth-child(even) { background: #f3f3f3; }
input, select { margin: 5px; padding: 5px; }
div.query-box { margin: 20px; padding: 10px; background: #f9f9f9; border-radius: 5px; }
div.form-row { display: inline-block; margin-right: 15px; }
.top-btn { min-width: 140px; height: 36px; padding: 0 15px; box-sizing: border-box; text-align: center; border: none; cursor: pointer; color: white; display: inline-block; font-size: 14px; margin: 0; line-height: 36px; border-radius: 4px; text-decoration: none; }
.bg-green { background: #4CAF50; }
.bg-blue { background: #2196F3; }
.bg-red { background: #f44336; }
</style>
<script>
var fnames = ['PatientName', 'PatientID', 'BirthDate', 'Sex', 'ReqPhys', 'ReqSvc', 'ProcDesc', 'Reason', 'Accession', 'ProcID', 'Priority', 'StationAE', 'StartDate', 'StartTime', 'PerfPhys', 'SPSDesc', 'SPSID', 'StationName', 'Location', 'Status', 'Modality', 'RefPhys', 'StudyDesc', 'ProtoCode', 'ProtoScheme', 'ProtoMeaning', 'StudyUID'];

function f(r) {
    var c = r.cells;
    for (var i = 0; i < c.length && i < fnames.length; i++) {
        var e = document.getElementsByName(fnames[i])[0];
        if (e) e.value = (c[i].innerText || c[i].textContent).trim();
    }
    var oa = document.getElementById('oldAcc');
    if (oa) oa.value = c[8].innerText.trim();
    var sel = document.getElementById('procCodeSel');
    if (sel) {
        var pid = document.getElementsByName('ProcID')[0].value;
        for (var j = 0; j < sel.options.length; j++) {
            sel.options[j].selected = (sel.options[j].getAttribute('data-id') === pid);
        }
    }
}

window.maxAcc = window.maxAcc || 0;

function nxtA() {
  var t = document.getElementById('pTable'), m = 0, p = 'ACC', c = 5;
  if (t) {
    for (var i = 1; i < t.rows.length; i++) {
      var v = t.rows[i].cells[8].innerText || '';
      var mt = v.match(/([^0-9]*)([0-9]+)/);
      if (mt) {
        p = mt[1];
        var val = parseInt(mt[2], 10);
        if (val > m) { m = val; c = mt[2].length; }
      }
    }
  }
  if (window.maxAcc <= m) window.maxAcc = m;
  window.maxAcc++;
  var nStr = String(window.maxAcc);
  while (nStr.length < c) nStr = '0' + nStr;
  document.getElementsByName('Accession')[0].value = p + nStr;
}

function sendMan() {
  try {
    var acc = document.getElementsByName('Accession')[0];
    var a = acc.value.trim();
    if (!a || a.indexOf('UNLINKED') === 0) { nxtA(); }
    
    var oa = document.getElementById('oldAcc');
    var oldA = oa ? encodeURIComponent(oa.value) : '';
    var rld = function() { window.location.href = '/'; };
    
    var sel = document.getElementById('procCodeSel');
    var opts = [];
    if (sel) {
      for (var j = 0; j < sel.options.length; j++) {
        if (sel.options[j].selected && sel.options[j].value !== '') {
          opts.push(sel.options[j]);
        }
      }
    }
    
    if (opts.length > 1) {
      var pArr = [];
      for (var k = 0; k < opts.length; k++) {
        if (k > 0) { nxtA(); }
        document.getElementsByName('Modality')[0].value = opts[k].getAttribute('data-mod') || '';
        document.getElementsByName('ProcDesc')[0].value = opts[k].text || '';
        document.getElementsByName('ProcID')[0].value = opts[k].getAttribute('data-id') || '';
        document.getElementsByName('SPSID')[0].value = opts[k].getAttribute('data-code') || '';
        document.getElementsByName('StationAE')[0].value = '';
        document.getElementsByName('SPSDesc')[0].value = '';
        var sn = document.getElementsByName('StationName')[0];
        if (sn) sn.value = '';
        document.getElementsByName('Location')[0].value = '';
        document.getElementsByName('StudyDesc')[0].value = '';
        
        var c1 = [];
        for (var i = 0; i < fnames.length; i++) {
          var el = document.getElementsByName(fnames[i])[0];
          var v = el ? el.value : '';
          if (v.indexOf(',') > -1 || v.indexOf('"') > -1) { 
            v = '"' + v.replace(new RegExp('"','g'), '""') + '"'; 
          }
          c1.push(v);
        }
        pArr.push(fetch('/manual?oldAcc=' + oldA, {method: 'POST', body: c1.join(',')}).catch(function(){}));
      }
      Promise.all(pArr).then(rld).catch(rld);
      return false;
    }
    
    var c2 = [];
    for (var i = 0; i < fnames.length; i++) {
      var el = document.getElementsByName(fnames[i])[0];
      var v = el ? el.value : '';
      if (v.indexOf(',') > -1 || v.indexOf('"') > -1) { 
        v = '"' + v.replace(new RegExp('"','g'), '""') + '"'; 
      }
      c2.push(v);
    }
    fetch('/manual?oldAcc=' + oldA, {method: 'POST', body: c2.join(',')}).then(rld).catch(rld);
  } catch(err) { 
    alert('UI Error: ' + err.message); 
  }
  return false;
}

function delP() {
  var a = document.getElementsByName('Accession')[0].value;
  if (a && confirm('Delete Patient ' + a + '?')) {
    var rld = function() { window.location.href = '/'; };
    fetch('/delpat', {method: 'POST', body: a}).then(rld).catch(rld);
  }
}

function delAll() {
    var p = prompt('Enter password to delete all data:');
    if (p !== null) {
        document.getElementById('delPwd').value = p;
        document.getElementById('delAllForm').submit();
    }
}

function multiStudy() {
    alert('Stub not implemented');
}

function sortTable(n) {
    var t = document.getElementById('pTable'), r, sw = true, i, x, y, sd, d = 'asc';
    while (sw) {
        sw = false;
        r = t.rows;
        for (i = 1; i < (r.length - 1); i++) {
            sd = false;
            x = r[i].getElementsByTagName('TD')[n];
            y = r[i + 1].getElementsByTagName('TD')[n];
            if (d == 'asc') {
                if (x.innerHTML.toLowerCase() > y.innerHTML.toLowerCase()) { sd = true; break; }
            } else {
                if (x.innerHTML.toLowerCase() < y.innerHTML.toLowerCase()) { sd = true; break; }
            }
        }
        if (sd) { r[i].parentNode.insertBefore(r[i + 1], r[i]); sw = true; }
    }
}

function formatDates() {
    var t = document.getElementById('pTable');
    if (!t) return;
    for (var i = 1; i < t.rows.length; i++) {
        var c = t.rows[i].cells;
        if (c.length > 13) {
            var b = c[2].innerText;
            if (b.length === 8) c[2].innerText = b.substr(0, 4) + '-' + b.substr(4, 2) + '-' + b.substr(6, 2);
            var s = c[12].innerText;
            if (s.length === 8) c[12].innerText = s.substr(0, 4) + '-' + s.substr(4, 2) + '-' + s.substr(6, 2);
            var tm = c[13].innerText;
            if (tm.length === 6) c[13].innerText = tm.substr(0, 2) + ':' + tm.substr(2, 2);
        }
    }
}

function newPatient() {
    var oa = document.getElementById('oldAcc');
    if (oa) oa.value = '';
    for (var i = 0; i < fnames.length; i++) {
        var el = document.getElementsByName(fnames[i])[0];
        if (el) el.value = '';
    }
    var d = new Date(), y = d.getFullYear(), mo = ('0' + (d.getMonth() + 1)).slice(-2), da = ('0' + d.getDate()).slice(-2);
    var sd = document.getElementsByName('StartDate')[0];
    if (sd) sd.value = y + '-' + mo + '-' + da;
    nxtA();
    
    var su = document.getElementsByName('StudyUID')[0];
    if (su) {
        var sec = Math.floor(Date.now() / 1000);
        var rnd = Math.floor(Math.random() * 10000);
        su.value = '1.2.840.113619.2.1.' + sec + '.' + rnd;
    }
    
    var stat = document.getElementsByName('Status')[0]; if (stat) stat.value = '1';
    var pri = document.getElementsByName('Priority')[0]; if (pri) pri.value = 'LOW';
    var tm = document.getElementsByName('StartTime')[0]; if (tm) tm.value = '16:00';
}

function loadProcCode() {
    var sel = document.getElementById('procCodeSel');
    if (sel && sel.selectedIndex >= 0) {
        var idx = sel.selectedIndex;
        var id = document.getElementsByName('ProcID')[0];
        var code = document.getElementsByName('SPSID')[0];
        var mod = document.getElementsByName('Modality')[0];
        var desc = document.getElementsByName('ProcDesc')[0];
        if (id && code && mod && desc) {
            id.value = sel.options[idx].getAttribute('data-id') || '';
            code.value = sel.options[idx].getAttribute('data-code') || '';
            mod.value = sel.options[idx].getAttribute('data-mod') || '';
            desc.value = sel.options[idx].text;
        }
    }
}

function filterProcCodes() {
    var f = document.getElementById('procFilter').value.toUpperCase();
    var s = document.getElementById('procCodeSel');
    var o = s.options;
    for (var i = 1; i < o.length; i++) {
        if ((o[i].text || o[i].innerText).toUpperCase().indexOf(f) > -1) {
            o[i].style.display = '';
        } else {
            o[i].style.display = 'none';
            o[i].selected = false;
        }
    }
}

let ptDebounce = null;
let ptAbort = null;

function filterPatients() {
    var f = document.getElementById('patientFilter').value.trim();
    if (ptDebounce) clearTimeout(ptDebounce);
    ptDebounce = setTimeout(async function() {
        if (ptAbort) ptAbort.abort();
        ptAbort = new AbortController();
        try {
            var res = await fetch('/api/patients?filter=' + encodeURIComponent(f), { signal: ptAbort.signal });
            if (!res.ok) return;
            var names = await res.json();
            var sel = document.getElementById('patientSel');
            sel.options.length = 0;
            names.forEach(function(n) {
                var opt = document.createElement('option');
                opt.value = n; opt.text = n;
                sel.appendChild(opt);
            });
        } catch (e) {}
    }, 250);
}

async function loadPatientDetails(name) {
    if (!name) return;
    try {
        var res = await fetch('/api/patient/' + encodeURIComponent(name));
        if (!res.ok) return;
        var data = await res.json();
        if (!data.error) {
            document.getElementsByName('PatientName')[0].value = data.name || '';
            document.getElementsByName('PatientID')[0].value = data.patient_id || '';
            var bd = data.birthdate || '';
            if (/^\d{8}$/.test(bd)) {
                bd = bd.substr(0, 4) + '-' + bd.substr(4, 2) + '-' + bd.substr(6, 2);
            } else if (bd.indexOf('/') > -1) {
                var p = bd.split('/');
                if (p.length === 3) {
                    if (p[2].length === 4) bd = p[2] + '-' + p[0].padStart(2, '0') + '-' + p[1].padStart(2, '0');
                    else if (p[0].length === 4) bd = p[0] + '-' + p[1].padStart(2, '0') + '-' + p[2].padStart(2, '0');
                }
            }
            document.getElementsByName('BirthDate')[0].value = bd;
            document.getElementsByName('Sex')[0].value = data.sex || '';
        }
    } catch (e) {}
}

function filterStatus(s) {
    var t = document.getElementById('pTable');
    if (!t) return;
    for (var i = 1; i < t.rows.length; i++) {
        var st = t.rows[i].cells[19].innerText || t.rows[i].cells[19].textContent;
        if (s === '' || st.trim() === s) t.rows[i].style.display = '';
        else t.rows[i].style.display = 'none';
    }
}

window.onload = function() {
    newPatient();
    filterPatients();
    filterStatus('1');
};
</script>
</head>
<body style='font-family:Arial,sans-serif;padding:20px;'>