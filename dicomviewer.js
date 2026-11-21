/* ============================================================================
 * Web DICOM Viewer (Embeddable JS Module with Responsive Mini-Toolbar)
 * ============================================================================ */
window.DicomViewer = (function () {
    let styleInjected = false;
    let activeCleanup = null;

    function injectStyles() {
        if (styleInjected) return;
        styleInjected = true;
        const st = document.createElement('style');
        st.textContent = `
            .dv-root { position:relative; display:flex; flex-direction:column; width:100%; height:100%; background:#000; color:#fff; font-family:sans-serif; overflow:hidden; }
            .dv-root:fullscreen { width:100vw; height:100vh; }
            .dv-tb { background:#333; padding:8px; display:flex; gap:8px; align-items:center; border-bottom:1px solid #555; flex-wrap:wrap; flex-shrink:0; }
            .dv-mini-tb { display:none; background:#2c3e50; padding:6px 10px; gap:8px; align-items:center; justify-content:space-between; border-bottom:1px solid #444; flex-shrink:0; font-size:12px; }
            .dv-mini-right { display:flex; gap:6px; align-items:center; }
            .dv-btn { cursor:pointer; background:#4CAF50; color:#fff; border:none; padding:5px 10px; border-radius:3px; font-weight:bold; text-decoration:none; font-size:12px; line-height:1.2; }
            .dv-btn:hover { opacity:0.9; }
            .dv-mini-btn { cursor:pointer; background:#34495e; color:#fff; border:1px solid #5d6d7e; padding:3px 9px; border-radius:3px; font-weight:bold; font-size:12px; }
            .dv-mini-btn:hover { background:#46627f; }
            .dv-main { display:flex; flex:1; min-height:0; overflow:hidden; }
            .dv-thumbs { width:150px; background:#1a1a1a; overflow-y:auto; padding:10px; flex-shrink:0; }
            .dv-thumb { cursor:pointer; text-align:center; margin-bottom:10px; padding:5px; border:2px solid transparent; border-radius:4px; background:#2a2a2a; }
            .dv-thumb:hover { background:#333; }
            .dv-thumb.sel { background:#3a4a6a; border-color:#5a7aFF; }
            .dv-thumb canvas { width:120px; height:120px; background:#000; object-fit:contain; display:block; margin:0 auto; }
            .dv-thumb-txt { font-size:12px; margin-bottom:4px; color:#ddd; text-overflow:ellipsis; white-space:nowrap; overflow:hidden; }
            .dv-view { flex:1; position:relative; overflow:hidden; background:#050505; }
            .dv-view canvas { display:block; }
            .dv-anno { position:absolute; color:#0f0; font-family:monospace; font-size:12px; font-weight:bold; text-shadow:1px 1px 0 #000, -1px -1px 0 #000, 1px -1px 0 #000, -1px 1px 0 #000; pointer-events:none; z-index:5; white-space:pre-line; }
            .dv-anno-tl { top:8px; left:8px; }
            .dv-anno-tr { top:8px; right:8px; text-align:right; }
            .dv-anno-bl { bottom:8px; left:8px; }
            .dv-anno-br { bottom:8px; right:8px; text-align:right; }
            .dv-hdr { display:none; position:absolute; right:0; top:0; width:340px; max-width:90%; height:100%; background:rgba(15,15,15,0.95); color:#0f0; font-family:monospace; padding:10px; overflow-y:auto; white-space:pre-wrap; font-size:12px; border-left:1px solid #444; box-sizing:border-box; z-index:10; }
            .dv-root.dv-compact .dv-tb,
            .dv-root.dv-compact .dv-thumbs { display:none !important; }
            .dv-root.dv-compact .dv-mini-tb { display:flex !important; }
        `;
        document.head.appendChild(st);
    }

    function mount(containerId, acc, opts = {}) {
        if (activeCleanup) { activeCleanup(); activeCleanup = null; }
        injectStyles();

        const host = typeof containerId === 'string' ? document.getElementById(containerId) : containerId;
        if (!host) return;

        let sop = opts.sop || '';
        let imgList = [], curIdx = -1, dicom = null;
        let panX = 0, panY = 0, zoom = 1, isPan = false, isWl = false, lastX = 0, lastY = 0;
        let boxMode = false, roiX = 0, roiY = 0, roiW = 100, roiH = 100, isDragBox = false;
        let isZoomDrag = false, showAnnotations = true, invertImg = false, lastInvert = false;
        let lastImgData = null, lastWc = null, lastWw = null;

        const metaText = (opts.seriesCount !== undefined && opts.instanceCount !== undefined)
            ? `(${opts.seriesCount} series, ${opts.instanceCount} images)`
            : '';

        host.innerHTML = `
        <div class="dv-root">
            <div class="dv-mini-tb">
                <span class="dv-mini-info" style="color:#d6eaf8; font-weight:bold;">${metaText}</span>
                <div class="dv-mini-right">
                    <button type="button" class="dv-mini-btn dv-act-fs" style="background:#16a085; border-color:#1abc9c;">Fullscreen</button>
                    <button type="button" class="dv-mini-btn dv-act-prev" title="Previous Image">&lt;</button>
                    <button type="button" class="dv-mini-btn dv-act-next" title="Next Image">&gt;</button>
                </div>
            </div>
            <div class="dv-tb">
                <button type="button" class="dv-btn dv-act-prev">&#9664; Prev</button>
                <button type="button" class="dv-btn dv-act-next">Next &#9654;</button>
                <button type="button" class="dv-btn dv-act-zin">Zoom In</button>
                <button type="button" class="dv-btn dv-act-zout">Zoom Out</button>
                <button type="button" class="dv-btn dv-act-reset" style="background:#2196F3;">Zoom Extents</button>
                <button type="button" class="dv-btn dv-act-box" style="background:#9c27b0;">ROI Box</button>
                <button type="button" class="dv-btn dv-act-anno" style="background:#ff9800;">Annotations</button>
                <button type="button" class="dv-btn dv-act-inv" style="background:#607d8b;">Invert</button>
                <a class="dv-btn dv-act-dl" href="#" target="_blank">Download</a>
                <button type="button" class="dv-btn dv-act-upl-btn">Upload</button>
                <input type="file" class="dv-act-upl" style="display:none;" accept=".dcm">
                <button type="button" class="dv-btn dv-act-rep-btn" style="background:#f44336;">Replace</button>
                <input type="file" class="dv-act-rep" style="display:none;" accept=".dcm">
                <button type="button" class="dv-btn dv-act-fs" style="background:#16a085;">Fullscreen</button>
                <span style="margin-left:auto;color:#aaa;font-size:11px;">1-5:LUT | B:Box | I:Invert | A:Anno | H:Header | Shift-Drag:Zoom | DblClick:Extents</span>
            </div>
            <div class="dv-main">
                <div class="dv-thumbs"></div>
                <div class="dv-view">
                    <canvas class="dv-cvs"></canvas>
                    <div class="dv-anno dv-anno-tl"></div>
                    <div class="dv-anno dv-anno-tr"></div>
                    <div class="dv-anno dv-anno-bl"></div>
                    <div class="dv-anno dv-anno-br"></div>
                    <div class="dv-hdr"></div>
                </div>
            </div>
        </div>`;

        const root = host.querySelector('.dv-root');
        const v = root.querySelector('.dv-view');
        const cvs = root.querySelector('.dv-cvs');
        const ctx = cvs.getContext('2d');
        const thDiv = root.querySelector('.dv-thumbs');
        const hdrEl = root.querySelector('.dv-hdr');
        const dlBtn = root.querySelector('.dv-act-dl');
        const boxBtn = root.querySelector('.dv-act-box');
        const miniInfo = root.querySelector('.dv-mini-info');
        const annoTL = root.querySelector('.dv-anno-tl');
        const annoTR = root.querySelector('.dv-anno-tr');
        const annoBL = root.querySelector('.dv-anno-bl');
        const annoBR = root.querySelector('.dv-anno-br');

        function updateLayout() {
            const isSmall = root.clientWidth < 720 || window.innerWidth < 720;
            root.classList.toggle('dv-compact', isSmall);
            root.querySelectorAll('.dv-act-fs').forEach(b => {
                b.innerText = document.fullscreenElement === root ? 'Exit Fullscreen' : 'Fullscreen';
            });
            if (dicom) resetView(); else drawEmpty();
        }

        function updateMiniCounter() {
            if (!miniInfo) return;
            if (imgList.length > 0 && curIdx >= 0) {
                const serSet = new Set(imgList.map(x => x.series || 1));
                miniInfo.innerText = `(${serSet.size} series, ${curIdx + 1}/${imgList.length} images)`;
            } else {
                miniInfo.innerText = metaText || '(0 images)';
            }
        }

        function toggleFullscreen() {
            if (document.fullscreenElement === root) {
                document.exitFullscreen?.();
            } else if (root.requestFullscreen) {
                root.requestFullscreen();
            }
        }

        function loadIdx(i) {
            if (curIdx >= 0) thDiv.querySelector(`#dv_t_${curIdx}`)?.classList.remove('sel');
            curIdx = i;
            const tEl = thDiv.querySelector(`#dv_t_${i}`);
            if (tEl) { tEl.classList.add('sel'); tEl.scrollIntoView({ behavior: 'smooth', block: 'nearest' }); }
            sop = imgList[i].sop;
            dlBtn.href = '/download?sop=' + encodeURIComponent(sop);
            updateMiniCounter();
            fetch('/download?sop=' + encodeURIComponent(sop)).then(r => r.arrayBuffer()).then(b => {
                dicom = parseDcm(b);
                hdrEl.innerText = dicom.hdr;
                dicom.dwc = dicom.wc; dicom.dww = dicom.ww;
                roiW = Math.max(dicom.w / 4, 50); roiH = Math.max(dicom.h / 4, 50);
                roiX = (dicom.w - roiW) / 2; roiY = (dicom.h - roiH) / 2;
                lastImgData = null; invertImg = false;
                updateStaticAnnotations();
                resetView();
            }).catch(console.error);
        }

        function nav(d) {
            const n = curIdx + d;
            if (n >= 0 && n < imgList.length) loadIdx(n);
        }

        function toggleBox() {
            boxMode = !boxMode;
            boxBtn.style.opacity = boxMode ? 0.6 : 1;
            if (boxMode) autoLevel(); else drawToCvs(cvs, dicom, false);
        }
        function toggleAnnotations() { showAnnotations = !showAnnotations; updateStaticAnnotations(); render(); }
        function toggleInvert() { invertImg = !invertImg; drawToCvs(cvs, dicom, false); }
        function zoomStep(zf) {
            if (!dicom) return;
            const w = dicom.w * zoom, h = dicom.h * zoom;
            panX -= (w * zf - w) / 2; panY -= (h * zf - h) / 2; zoom *= zf;
            render();
        }

        function updateStaticAnnotations() {
            if (!dicom || !showAnnotations) {
                annoTL.innerText = ''; annoTR.innerText = ''; annoBL.innerText = ''; return;
            }
            const fd = (dicom.date && dicom.date.length >= 8) ? dicom.date.substring(0, 4) + '-' + dicom.date.substring(4, 6) + '-' + dicom.date.substring(6, 8) : dicom.date;
            const ft = (dicom.time && dicom.time.length >= 6) ? dicom.time.substring(0, 2) + ':' + dicom.time.substring(2, 4) + ':' + dicom.time.substring(4, 6) : dicom.time;
            annoTL.innerText = [dicom.pName, dicom.pId, dicom.pDob].filter(Boolean).join('\n');
            annoTR.innerText = [dicom.site, fd, ft, dicom.desc, dicom.tech].filter(Boolean).join('\n');
            const bl = [];
            if (dicom.kvp) bl.push('kVp: ' + dicom.kvp);
            if (dicom.ma) bl.push('mA: ' + dicom.ma);
            if (dicom.mas) bl.push('mAs: ' + dicom.mas);
            if (dicom.dose) bl.push('Dose: ' + dicom.dose);
            if (dicom.st) bl.push('Thick: ' + dicom.st);
            if (dicom.tr) bl.push('TR: ' + dicom.tr);
            if (dicom.te) bl.push('TE: ' + dicom.te);
            annoBL.innerText = bl.join('\n');
        }

        function autoLevel() {
            if (!dicom || !boxMode) return;
            let min = 99999, max = -99999;
            const dv = new DataView(dicom.px.buffer, dicom.px.byteOffset, dicom.px.byteLength);
            const blen = dicom.px.byteLength;
            for (let y = Math.floor(roiY); y < roiY + roiH; y++) {
                if (y < 0 || y >= dicom.h) continue;
                for (let x = Math.floor(roiX); x < roiX + roiW; x++) {
                    if (x < 0 || x >= dicom.w) continue;
                    const srcIdx = y * dicom.w + x;
                    const offset = (dicom.b === 8) ? srcIdx : srcIdx * 2;
                    let raw = 0;
                    if (offset + (dicom.b === 8 ? 1 : 2) <= blen) {
                        raw = (dicom.b === 8) ? dv.getUint8(offset) : (dicom.pr === 1 ? dv.getInt16(offset, !dicom.isBE) : dv.getUint16(offset, !dicom.isBE));
                    }
                    const val = raw * dicom.rs + dicom.ri;
                    if (val < min) min = val;
                    if (val > max) max = val;
                }
            }
            if (min <= max) { dicom.ww = Math.max(1, max - min); dicom.wc = min + dicom.ww / 2; }
            drawToCvs(cvs, dicom, false);
        }

        function parseDcm(buf) {
            const dv = new DataView(buf), u8 = new Uint8Array(buf);
            let off = 0;
            const t = {
                w: 0, h: 0, b: 0, wc: null, ww: null, rs: 1, ri: 0, px: null, dwc: 2048, dww: 4096,
                pm: '', pr: -1, hdr: '', isBE: false, samples: 0, isJpeg: false,
                pName: '', pId: '', pDob: '', site: '', date: '', time: '', desc: '',
                kvp: '', ma: '', mas: '', dose: '', tech: '', st: '', tr: '', te: ''
            };
            const addHdr = (s) => { t.hdr += s + '\n'; };

            const geOffs = [3228, 3240, 0, 3178, 3180];
            for (const o of geOffs) {
                if (o + 4 <= buf.byteLength && String.fromCharCode(u8[o], u8[o + 1], u8[o + 2], u8[o + 3]) === 'IMGF') {
                    const imgOff = dv.getUint32(o + 4, false);
                    t.w = dv.getUint32(o + 8, false); t.h = dv.getUint32(o + 12, false); t.b = dv.getUint32(o + 16, false);
                    t.px = new Uint8Array(buf, o + imgOff, t.w * t.h * (t.b / 8));
                    t.isBE = true; t.samples = 1; t.pm = 'MONOCHROME2'; t.pr = 0;
                    addHdr('Format: GE Genesis (IMGF)\nMatrix: ' + t.w + ' x ' + t.h + ' x ' + t.b + '-bits');
                    return t;
                }
            }

            if (buf.byteLength >= 6144) {
                const s96 = String.fromCharCode(...u8.slice(96, 103)), s0 = String.fromCharCode(...u8.slice(0, 7));
                const s281 = buf.byteLength > 300 ? String.fromCharCode(...u8.slice(281, 288)) : '';
                if (s96 === 'SIEMENS') {
                    t.w = dv.getUint32(2864, false); t.h = t.w; t.b = 16; t.isBE = true; t.samples = 1; t.pm = 'MONOCHROME2'; t.pr = 0;
                    if (t.w == 0 || t.w > 2048) { t.w = buf.byteLength >= 6144 + 512 * 512 * 2 ? 512 : 256; t.h = t.w; }
                    t.px = new Uint8Array(buf, 6144, t.w * t.h * 2); addHdr('Format: Siemens Vision'); return t;
                }
                if (buf.byteLength === 530432 || s281 === 'SOMATOM') {
                    t.w = 512; t.h = 512; t.b = 16; t.isBE = true; t.samples = 1; t.pm = 'MONOCHROME2'; t.pr = 0;
                    t.px = new Uint8Array(buf, 6144, t.w * t.h * 2); addHdr('Format: Siemens Somatom'); return t;
                }
                if (s0 === 'SIEMENS') {
                    t.w = dv.getUint32(5340, false); t.h = dv.getUint32(5344, false); t.b = 16; t.isBE = true; t.samples = 1; t.pm = 'MONOCHROME2'; t.pr = 0;
                    t.px = new Uint8Array(buf, 8192, t.w * t.h * 2); addHdr('Format: Siemens Plus'); return t;
                }
            }

            if (u8[0] === 80 && (u8[1] === 53 || u8[1] === 54)) {
                const type = u8[1] - 48, tok = [];
                let pos = 2;
                while (tok.length < 3 && pos < buf.byteLength) {
                    while (pos < buf.byteLength && u8[pos] <= 32) pos++;
                    if (u8[pos] === 35) { while (pos < buf.byteLength && u8[pos] !== 10) pos++; continue; }
                    let val = 0, start = pos;
                    while (pos < buf.byteLength && u8[pos] >= 48 && u8[pos] <= 57) { val = val * 10 + (u8[pos] - 48); pos++; }
                    if (pos > start) tok.push(val);
                }
                if (pos < buf.byteLength && u8[pos] <= 32) pos++;
                if (tok.length === 3) {
                    t.w = tok[0]; t.h = tok[1]; t.b = tok[2] > 255 ? 16 : 8; t.samples = type === 6 ? 3 : 1;
                    t.pm = type === 6 ? 'RGB' : 'MONOCHROME2'; t.pr = 0; t.isBE = true; t.px = new Uint8Array(buf, pos);
                    addHdr('Format: Netpbm P' + type + '\nMatrix: ' + t.w + ' x ' + t.h); return t;
                }
            }

            if (buf.byteLength >= 132 && String.fromCharCode(u8[128], u8[129], u8[130], u8[131]) === 'DICM') off = 132;
            else if (String.fromCharCode(u8[0], u8[1], u8[2], u8[3]) === 'DICM') off = 4;

            let isExp = true, isBE = false;
            if (off === 0) {
                const gle = dv.getUint16(0, true), gbe = dv.getUint16(0, false);
                if ([2, 8, 32, 40].includes(gle)) { isBE = false; isExp = (u8[4] >= 65 && u8[4] <= 90 && u8[5] >= 65 && u8[5] <= 90); }
                else if ([2, 8, 32, 40].includes(gbe)) { isBE = true; isExp = (u8[4] >= 65 && u8[4] <= 90 && u8[5] >= 65 && u8[5] <= 90); }
            }
            addHdr('Mode: DICOM ' + (isBE ? '(Big-Endian)' : '(Little-Endian)'));
            addHdr('-----------------------------------');

            while (off + 8 <= buf.byteLength) {
                const le = !isBE;
                const g = dv.getUint16(off, le), e = dv.getUint16(off + 2, le);
                off += 4;
                let len = 0, curExp = isExp;
                if (g === 0x0002) curExp = true;
                if (g === 0xFFFE) curExp = false;

                let vr = '--';
                if (curExp) {
                    const c1 = String.fromCharCode(u8[off]), c2 = String.fromCharCode(u8[off + 1]);
                    if (/[A-Z]{2}/.test(c1 + c2)) {
                        vr = c1 + c2; off += 2;
                        if (['OB', 'OW', 'OF', 'SQ', 'UT', 'UN', 'UR', 'UC', 'OD', 'OL'].includes(vr)) {
                            off += 2; len = dv.getUint32(off, g === 2 ? true : le); off += 4;
                        } else {
                            len = dv.getUint16(off, g === 2 ? true : le); off += 2;
                        }
                    } else { len = dv.getUint32(off, g === 2 ? true : le); off += 4; }
                } else { len = dv.getUint32(off, g === 2 ? true : le); off += 4; }

                const origLen = len;
                if (len === 0xFFFFFFFF) len = 0;

                if (g === 0x7FE0 && e === 0x0010) {
                    if (origLen === 0xFFFFFFFF) {
                        const botLen = dv.getUint32(off + 4, le);
                        const fragOff = off + 8 + botLen;
                        if (fragOff + 8 <= buf.byteLength) {
                            const fg = dv.getUint16(fragOff, le), fe = dv.getUint16(fragOff + 2, le), fLen = dv.getUint32(fragOff + 4, le);
                            if (fg === 0xFFFE && fe === 0xE000 && fLen > 0 && fragOff + 8 + fLen <= buf.byteLength) {
                                t.isJpeg = true; t.b = 8; t.samples = 3; t.pm = 'RGB';
                                t.px = new Uint8Array(buf, fragOff + 8, fLen); t.isBE = isBE; break;
                            }
                        }
                    }
                    const pxLen = (origLen === 0xFFFFFFFF || origLen === 0) ? buf.byteLength - off : Math.min(origLen, buf.byteLength - off);
                    t.px = new Uint8Array(buf, off, pxLen); t.isBE = isBE; break;
                }

                let vStr = '';
                if (len > 0 && len < 256 && off + len <= buf.byteLength) {
                    for (let k = 0; k < len; k++) { const c = u8[off + k]; if (c > 0) vStr += String.fromCharCode(c); }
                    vStr = vStr.trim();
                }

                const hex = (n) => n.toString(16).padStart(4, '0').toUpperCase();
                addHdr('(' + hex(g) + ',' + hex(e) + ') VR: ' + vr + ' Len: ' + origLen + (vStr ? ' [' + vStr + ']' : ''));

                if (g === 0x0002 && e === 0x0010) {
                    if (vStr === '1.2.840.10008.1.2') { isExp = false; isBE = false; }
                    else if (vStr === '1.2.840.10008.1.2.2' || vStr === '1.2.840.113619.5.2') { isExp = true; isBE = true; }
                    else if (vStr === '1.2.840.10008.1.2.1') { isExp = true; isBE = false; }
                }
                else if (g === 0x0010 && e === 0x0010) t.pName = vStr.replace(/\^/g, ' ');
                else if (g === 0x0010 && e === 0x0020) t.pId = vStr;
                else if (g === 0x0010 && e === 0x0030) t.pDob = vStr;
                else if (g === 0x0008 && e === 0x0080) t.site = vStr;
                else if (g === 0x0008 && e === 0x0020) t.date = vStr;
                else if (g === 0x0008 && e === 0x0030) t.time = vStr;
                else if (g === 0x0008 && e === 0x103E) t.desc = vStr;
                else if (g === 0x0018 && e === 0x0060) t.kvp = vStr;
                else if (g === 0x0018 && e === 0x1151) t.ma = vStr;
                else if (g === 0x0018 && e === 0x1152) t.mas = vStr;
                else if (g === 0x0018 && e === 0x115E) t.dose = vStr;
                else if (g === 0x0008 && e === 0x1070) t.tech = vStr.replace(/\^/g, ' ');
                else if (g === 0x0018 && e === 0x0050) t.st = vStr;
                else if (g === 0x0018 && e === 0x0080) t.tr = vStr;
                else if (g === 0x0018 && e === 0x0081) t.te = vStr;
                else if (g === 0x0028 && e === 0x0002 && t.samples === 0) t.samples = dv.getUint16(off, le);
                else if (g === 0x0028 && e === 0x0004 && t.pm === '') t.pm = vStr;
                else if (g === 0x0028 && e === 0x0010 && t.h === 0) t.h = dv.getUint16(off, le);
                else if (g === 0x0028 && e === 0x0011 && t.w === 0) t.w = dv.getUint16(off, le);
                else if (g === 0x0028 && e === 0x0100 && t.b === 0) t.b = dv.getUint16(off, le);
                else if (g === 0x0028 && e === 0x0103 && t.pr === -1) t.pr = dv.getUint16(off, le);
                else if (g === 0x0028 && e === 0x1050 && t.wc === null && vStr) { const nv = parseFloat(vStr.split('\\')[0]); if (!isNaN(nv)) t.wc = nv; }
                else if (g === 0x0028 && e === 0x1051 && t.ww === null && vStr) { const nv = parseFloat(vStr.split('\\')[0]); if (!isNaN(nv)) t.ww = nv; }
                else if (g === 0x0028 && e === 0x1052 && vStr) { const nv = parseFloat(vStr); if (!isNaN(nv)) t.ri = nv; }
                else if (g === 0x0028 && e === 0x1053 && vStr) { const nv = parseFloat(vStr); if (!isNaN(nv)) t.rs = nv; }

                if (origLen !== 0xFFFFFFFF) off += len;
            }

            if (t.samples === 0) t.samples = 1;
            if (t.b === 0) t.b = 16;
            if (t.pm === '') t.pm = 'MONOCHROME2';
            if (t.pr === -1) t.pr = 0;
            if (isNaN(t.wc) || t.wc === null || isNaN(t.ww) || t.ww === null) {
                if (t.b > 8) { t.wc = 2048; t.ww = 4096; } else { t.wc = 128; t.ww = 256; }
            }
            t.dwc = t.wc; t.dww = t.ww;
            return t;
        }

        function drawToCvs(c, t, isThumb) {
            if (!t || !t.px || !t.w || !t.h) return;
            const cx = c.getContext('2d');
            const dv = new DataView(t.px.buffer, t.px.byteOffset, t.px.byteLength);
            let isMono1 = (t.pm === 'MONOCHROME1');
            if (invertImg) isMono1 = !isMono1;
            const minV = t.wc - 0.5 - (t.ww - 1) / 2, maxV = t.wc - 0.5 + (t.ww - 1) / 2;
            const blen = t.px.byteLength;

            if (t.isJpeg) {
                const blob = new Blob([t.px], { type: 'image/jpeg' });
                const img = new Image();
                img.onload = () => {
                    if (isThumb) { cx.drawImage(img, 0, 0, t.w, t.h, 0, 0, c.width, c.height); }
                    else {
                        c.width = t.w; c.height = t.h; cx.drawImage(img, 0, 0);
                        if (boxMode) { cx.strokeStyle = 'yellow'; cx.lineWidth = 2 / zoom; cx.strokeRect(roiX, roiY, roiW, roiH); }
                        render();
                    }
                    URL.revokeObjectURL(img.src);
                };
                img.src = URL.createObjectURL(blob);
                return;
            }

            if (isThumb) {
                const tw = c.width, th = c.height, imgData = cx.createImageData(tw, th);
                const stepX = t.w / tw, stepY = t.h / th;
                for (let y = 0; y < th; y++) {
                    const srcY = Math.floor(y * stepY);
                    for (let x = 0; x < tw; x++) {
                        const srcX = Math.floor(x * stepX), srcIdx = srcY * t.w + srcX;
                        const offset = (t.b === 8) ? srcIdx : srcIdx * 2;
                        if (t.samples === 3 && t.b === 8) {
                            const pidx = srcIdx * 3;
                            if (pidx + 2 < blen) {
                                const d = (y * tw + x) * 4;
                                imgData.data[d] = invertImg ? 255 - t.px[pidx] : t.px[pidx];
                                imgData.data[d + 1] = invertImg ? 255 - t.px[pidx + 1] : t.px[pidx + 1];
                                imgData.data[d + 2] = invertImg ? 255 - t.px[pidx + 2] : t.px[pidx + 2];
                                imgData.data[d + 3] = 255;
                            }
                            continue;
                        }
                        let raw = 0;
                        if (offset + (t.b === 8 ? 1 : 2) <= blen) {
                            raw = (t.b === 8) ? dv.getUint8(offset) : (t.pr === 1 ? dv.getInt16(offset, !t.isBE) : dv.getUint16(offset, !t.isBE));
                        }
                        const val = raw * t.rs + t.ri;
                        let p = val <= minV ? 0 : val > maxV ? 255 : ((val - (t.wc - 0.5)) / (t.ww - 1)) * 255;
                        if (isMono1) p = 255 - p;
                        const d = (y * tw + x) * 4;
                        imgData.data[d] = p; imgData.data[d + 1] = p; imgData.data[d + 2] = p; imgData.data[d + 3] = 255;
                    }
                }
                cx.putImageData(imgData, 0, 0);
                return;
            }

            let imgData;
            if (lastImgData && lastImgData.width === t.w && lastImgData.height === t.h && lastWc === t.wc && lastWw === t.ww && lastInvert === invertImg) {
                imgData = lastImgData;
            } else {
                imgData = cx.createImageData(t.w, t.h);
                for (let i = 0; i < t.w * t.h; i++) {
                    if (t.samples === 3 && t.b === 8) {
                        const pidx = i * 3, idx = i * 4;
                        if (pidx + 2 < blen) {
                            imgData.data[idx] = invertImg ? 255 - t.px[pidx] : t.px[pidx];
                            imgData.data[idx + 1] = invertImg ? 255 - t.px[pidx + 1] : t.px[pidx + 1];
                            imgData.data[idx + 2] = invertImg ? 255 - t.px[pidx + 2] : t.px[pidx + 2];
                            imgData.data[idx + 3] = 255;
                        }
                        continue;
                    }
                    const offset = (t.b === 8) ? i : i * 2;
                    let raw = 0;
                    if (offset + (t.b === 8 ? 1 : 2) <= blen) {
                        raw = (t.b === 8) ? dv.getUint8(offset) : (t.pr === 1 ? dv.getInt16(offset, !t.isBE) : dv.getUint16(offset, !t.isBE));
                    }
                    const val = raw * t.rs + t.ri;
                    let p = val <= minV ? 0 : val > maxV ? 255 : ((val - (t.wc - 0.5)) / (t.ww - 1)) * 255;
                    if (isMono1) p = 255 - p;
                    const idx = i * 4;
                    imgData.data[idx] = p; imgData.data[idx + 1] = p; imgData.data[idx + 2] = p; imgData.data[idx + 3] = 255;
                }
                lastImgData = imgData; lastWc = t.wc; lastWw = t.ww; lastInvert = invertImg;
            }
            c.width = t.w; c.height = t.h; cx.putImageData(imgData, 0, 0);
            if (boxMode) { cx.strokeStyle = 'yellow'; cx.lineWidth = 2 / zoom; cx.strokeRect(roiX, roiY, roiW, roiH); }
            render();
        }

        function drawEmpty() {
            cvs.style.transform = 'none';
            cvs.width = Math.max(1, v.clientWidth);
            cvs.height = Math.max(1, v.clientHeight);
            ctx.clearRect(0, 0, cvs.width, cvs.height);
            ctx.fillStyle = 'gray';
            ctx.font = 'bold 13px sans-serif';
            const txt = 'Drag & Drop a DICOM (.dcm) file here';
            ctx.fillText(txt, (cvs.width - ctx.measureText(txt).width) / 2, cvs.height / 2);
            root.querySelectorAll('.dv-anno').forEach(el => { el.innerText = ''; });
        }

        function resetView() {
            if (!dicom || !v.clientWidth || !v.clientHeight) return;
            zoom = Math.min(v.clientWidth / dicom.w, v.clientHeight / dicom.h) * 0.95;
            panX = (v.clientWidth - dicom.w * zoom) / 2;
            panY = (v.clientHeight - dicom.h * zoom) / 2;
            drawToCvs(cvs, dicom, false);
        }

        function render() {
            if (!dicom) { drawEmpty(); return; }
            cvs.style.transformOrigin = '0 0';
            cvs.style.transform = 'translate(' + panX + 'px,' + panY + 'px) scale(' + zoom + ')';
            annoBR.innerText = showAnnotations
                ? `W: ${Math.round(dicom.ww)} L: ${Math.round(dicom.wc)}\nZoom: ${Math.round(zoom * 100)}%\n${dicom.w}x${dicom.h} (${dicom.b}-bit ${dicom.pr === 1 ? 'S' : 'U'})`
                : '';
        }

        function uploadFile(fi, isRep) {
            if (!fi.files.length) return;
            const fd = new FormData();
            fd.append('file', fi.files[0]);
            if (confirm('Write Worklist data into this file header?')) {
                fd.append('rewrite', '1');
                fd.append('acc', acc);
            }
            const url = isRep ? '/replace?sop=' + encodeURIComponent(sop) : '/upload_new?acc=' + encodeURIComponent(acc);
            fetch(url, { method: 'POST', body: fd }).then(() => mount(host, acc, opts)).catch(console.error);
        }

        root.querySelectorAll('.dv-act-prev').forEach(b => b.addEventListener('click', () => nav(-1)));
        root.querySelectorAll('.dv-act-next').forEach(b => b.addEventListener('click', () => nav(1)));
        root.querySelectorAll('.dv-act-fs').forEach(b => b.addEventListener('click', toggleFullscreen));
        root.querySelector('.dv-act-zin').addEventListener('click', () => zoomStep(1.1));
        root.querySelector('.dv-act-zout').addEventListener('click', () => zoomStep(0.9));
        root.querySelector('.dv-act-reset').addEventListener('click', resetView);
        boxBtn.addEventListener('click', toggleBox);
        root.querySelector('.dv-act-anno').addEventListener('click', toggleAnnotations);
        root.querySelector('.dv-act-inv').addEventListener('click', toggleInvert);
        const uplIn = root.querySelector('.dv-act-upl'), repIn = root.querySelector('.dv-act-rep');
        root.querySelector('.dv-act-upl-btn').addEventListener('click', () => uplIn.click());
        root.querySelector('.dv-act-rep-btn').addEventListener('click', () => repIn.click());
        uplIn.addEventListener('change', () => uploadFile(uplIn, false));
        repIn.addEventListener('change', () => uploadFile(repIn, true));

        v.ondblclick = () => resetView();
        v.addEventListener('dragover', e => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; });
        v.addEventListener('drop', e => {
            e.preventDefault();
            if (e.dataTransfer.files.length > 0) {
                const f = e.dataTransfer.files[0], r = new FileReader();
                r.onload = evt => {
                    dicom = parseDcm(evt.target.result);
                    dicom.hdr = 'File: ' + f.name + '\n' + dicom.hdr;
                    hdrEl.innerText = dicom.hdr;
                    dicom.dwc = dicom.wc; dicom.dww = dicom.ww;
                    roiW = Math.max(dicom.w / 4, 50); roiH = Math.max(dicom.h / 4, 50);
                    roiX = (dicom.w - roiW) / 2; roiY = (dicom.h - roiH) / 2;
                    curIdx = -1;
                    thDiv.querySelectorAll('.dv-thumb.sel').forEach(el => el.classList.remove('sel'));
                    lastImgData = null; invertImg = false;
                    updateStaticAnnotations();
                    resetView();
                };
                r.readAsArrayBuffer(f);
            }
        });

        v.onmousedown = e => {
            const r = cvs.getBoundingClientRect(), mx = (e.clientX - r.left) / zoom, my = (e.clientY - r.top) / zoom;
            if (e.button === 0) {
                if (e.shiftKey) isZoomDrag = true;
                else if (boxMode && mx >= roiX && mx <= roiX + roiW && my >= roiY && my <= roiY + roiH) isDragBox = true;
                else isPan = true;
            }
            if (e.button === 2) isWl = true;
            lastX = e.clientX; lastY = e.clientY;
        };
        v.onwheel = e => {
            if (!dicom) return;
            e.preventDefault();
            zoomStep(e.deltaY < 0 ? 1.1 : 0.9);
        };
        v.oncontextmenu = e => e.preventDefault();

        const onMouseUp = () => { isPan = false; isWl = false; isDragBox = false; isZoomDrag = false; };
        const onMouseMove = e => {
            if (!dicom) return;
            if (isDragBox) { roiX += (e.clientX - lastX) / zoom; roiY += (e.clientY - lastY) / zoom; lastX = e.clientX; lastY = e.clientY; autoLevel(); }
            else if (isZoomDrag) { const dy = lastY - e.clientY; if (Math.abs(dy) > 2) { zoomStep(dy > 0 ? 1.05 : 0.95); lastY = e.clientY; } }
            else if (isPan) { panX += (e.clientX - lastX); panY += (e.clientY - lastY); lastX = e.clientX; lastY = e.clientY; render(); }
            else if (isWl) { dicom.ww += (e.clientX - lastX) * 2; dicom.wc += (e.clientY - lastY) * 2; if (dicom.ww < 1) dicom.ww = 1; lastX = e.clientX; lastY = e.clientY; drawToCvs(cvs, dicom, false); }
        };
        const onKeyDown = e => {
            const tag = document.activeElement?.tagName;
            if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
            if (e.key === 'ArrowLeft') nav(-1);
            if (e.key === 'ArrowRight') nav(1);
            if (!dicom) return;
            if (e.key === '1') { dicom.wc = dicom.dwc; dicom.ww = dicom.dww; drawToCvs(cvs, dicom, false); }
            if (e.key === '2') { dicom.wc = -600; dicom.ww = 1500; drawToCvs(cvs, dicom, false); }
            if (e.key === '3') { dicom.wc = 300; dicom.ww = 1500; drawToCvs(cvs, dicom, false); }
            if (e.key === '4') { dicom.wc = 40; dicom.ww = 80; drawToCvs(cvs, dicom, false); }
            if (e.key === '5') { dicom.wc = 50; dicom.ww = 400; drawToCvs(cvs, dicom, false); }
            if (e.key.toLowerCase() === 'b') toggleBox();
            if (e.key.toLowerCase() === 'a') toggleAnnotations();
            if (e.key.toLowerCase() === 'i') toggleInvert();
            if (e.key.toLowerCase() === 'h') hdrEl.style.display = (hdrEl.style.display === 'none' || hdrEl.style.display === '') ? 'block' : 'none';
            if (e.key.toLowerCase() === 'r') resetView();
        };

        window.addEventListener('mouseup', onMouseUp);
        window.addEventListener('mousemove', onMouseMove);
        document.addEventListener('keydown', onKeyDown);
        document.addEventListener('fullscreenchange', updateLayout);
        window.addEventListener('resize', updateLayout);

        let ro = null;
        if (window.ResizeObserver) {
            ro = new ResizeObserver(updateLayout);
            ro.observe(root);
        }

        activeCleanup = () => {
            window.removeEventListener('mouseup', onMouseUp);
            window.removeEventListener('mousemove', onMouseMove);
            document.removeEventListener('keydown', onKeyDown);
            document.removeEventListener('fullscreenchange', updateLayout);
            window.removeEventListener('resize', updateLayout);
            if (ro) ro.disconnect();
        };

        if (acc) {
            fetch('/api/images?acc=' + encodeURIComponent(acc)).then(r => r.json()).then(lst => {
                imgList = Array.isArray(lst) ? lst : [];
                imgList.forEach((img, i) => {
                    const d = document.createElement('div');
                    d.className = 'dv-thumb'; d.id = 'dv_t_' + i; d.onclick = () => loadIdx(i);
                    d.innerHTML = `<div class="dv-thumb-txt">${img.mod || 'OT'}</div><canvas id="dv_tc_${i}" width="120" height="120"></canvas>`;
                    thDiv.appendChild(d);
                    if (img.sop === sop) loadIdx(i);
                    fetch('/download?sop=' + encodeURIComponent(img.sop)).then(r => r.arrayBuffer()).then(b => {
                        const t = parseDcm(b);
                        const tc = thDiv.querySelector(`#dv_tc_${i}`);
                        if (tc) drawToCvs(tc, t, true);
                    }).catch(console.error);
                });
                if (curIdx === -1 && imgList.length > 0) loadIdx(0);
                else updateMiniCounter();
            }).catch(console.error);
        } else if (sop) {
            imgList = [{ sop, mod: 'UNLINKED', series: 1 }];
            loadIdx(0);
        }

        updateLayout();
    }

    return { mount };
})();