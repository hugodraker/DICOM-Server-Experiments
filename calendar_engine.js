// ============================================================================
//   Reusable Calendar & Automated Weekly Scheduling Engine
//   Adapter-driven: works with SQLite (/api/query), CalDAV, or any backend.
// ============================================================================
window.CalendarEngine = (function () {let adapter = null;
    let zoom = 1.0;
    let viewMode = 3; // 1=Day, 2=4Day, 3=Week, 4=People, 6=Month, 7=Events, 8=Schedule
    let currentDate = new Date();
    let selectedEventId = null;

    let people = [];          // [{ id, name, meta }]
    let events = [];          // [{ id, personIdx, date:'YYYY/MM/DD', startMin, duration, title, color, meta }]
    let visiblePeopleIndices = [];
    let unselectedSchedUsers = new Set();
    let schedUserData = {};
let currentSchedYear = new Date().getFullYear();
    let currentSortCol = 'user', currentSortDir = 1;

    let isDragging = false, isResizing = false, isMonthDragging = false;
    let activeCard = null, activeEv = null, monthDragEv = null;
    let startMouseY = 0, startMouseX = 0, originalDateStr = "", originalStartMin = 0, originalDuration = 0;
    let hasMoved = false, lastTouchDblTapTime = 0;
    window.isEditing = false;
    window.isScheduling = false;

    const colors = ["#039BE5", "#33B679", "#8E24AA", "#F4511E", "#E67C73", "#00897B", "#3949AB", "#6D4C41"];

    // DOM refs (bound in init)
    let scrollArea, headerRow, timelineLabels, gridBg, eventsLayer, lblDateTitle;

    function isEditMode() {
        const chk = document.getElementById('chkEditMode');
        return chk && chk.checked;
    }

    function isOffEvent(e) {
        const t = (e.title || "").toUpperCase();
        const st = (e.meta && e.meta.shift_type) ? e.meta.shift_type.toUpperCase() : "";
        return st.startsWith("OFF") || t.includes("OFF");
    }

    function formatDate(d) {
        return `${d.getFullYear()}/${String(d.getMonth() + 1).padStart(2, '0')}/${String(d.getDate()).padStart(2, '0')}`;
    }

    function minToTimeStr(min) {
        let h = Math.floor(min / 60), m = min % 60;
        let displayH = h % 24;
        return `${displayH > 12 ? displayH - 12 : (displayH === 0 ? 12 : displayH)}:${String(m).padStart(2, '0')} ${displayH >= 12 ? 'PM' : 'AM'}`;
    }

    function getClientY(e) { return (e.touches?.[0] || e.changedTouches?.[0] || e).clientY; }
    function getClientX(e) { return (e.touches?.[0] || e.changedTouches?.[0] || e).clientX; }

    function bindDoubleTap(el, handler) {
        let lastTapTime = 0, lastTapX = 0, lastTapY = 0, startX = 0, startY = 0, moved = false;
        el.addEventListener('touchstart', (e) => {
            if (e.touches && e.touches.length > 1) { moved = true; return; }
            startX = getClientX(e); startY = getClientY(e); moved = false;
        }, { passive: true });
        el.addEventListener('touchmove', (e) => {
            if (Math.abs(getClientX(e) - startX) > 10 || Math.abs(getClientY(e) - startY) > 10) moved = true;
        }, { passive: true });
        el.addEventListener('touchend', (e) => {
            if (moved) return;
            let now = Date.now(), x = getClientX(e), y = getClientY(e);
            if (now - lastTapTime < 350 && Math.abs(x - lastTapX) < 30 && Math.abs(y - lastTapY) < 30) {
                if (e.cancelable) e.preventDefault();
                e.stopPropagation();
                lastTapTime = 0; lastTouchDblTapTime = now;
                isDragging = isResizing = isMonthDragging = false;
                activeCard = activeEv = monthDragEv = null;
                handler(e);
            } else { lastTapTime = now; lastTapX = x; lastTapY = y; }
        }, { passive: false });
        el.addEventListener('dblclick', (e) => {
            if (Date.now() - lastTouchDblTapTime < 500) return;
            handler(e);
        });
    }

    function getColCount() {
        if (viewMode === 1) return 1;
        if (viewMode === 2) return 4;
        if (viewMode === 3) return 7;
        if (viewMode === 4) {
            let offset = window.innerWidth <= 768 ? 40 : 65;
            let w = (scrollArea ? scrollArea.clientWidth : window.innerWidth) - offset;
            return Math.max(1, Math.min(255, Math.floor(w / 120)));
        }
        return 7;
    }

    function isPeopleView() { return viewMode === 4; }

    function getEffectiveVisiblePeople() {
        let sel = document.getElementById('comboTempPersonFilter');
        if (sel && sel.value !== "") return [parseInt(sel.value, 10)];
        return visiblePeopleIndices.length > 0 ? visiblePeopleIndices : people.map((_, i) => i);
    }

    function updateDateTitle(text) {
        if (lblDateTitle) lblDateTitle.innerText = text;
        const m = document.getElementById('lblDateTitleMobile');
        if (m) m.innerText = text;
    }

    function populateTempFilter() {
        let sel = document.getElementById('comboTempPersonFilter');
        if (!sel) return;
        let cur = sel.value;
        sel.innerHTML = '<option value="">All Visible</option>';
        visiblePeopleIndices.forEach(idx => {
            if (!people[idx]) return;
            let opt = document.createElement('option');
            opt.value = idx; opt.innerText = people[idx].name;
            sel.appendChild(opt);
        });
        if (cur && sel.querySelector(`option[value="${cur}"]`)) sel.value = cur;
    }

    async function refresh() {
        if (isDragging || isResizing || isMonthDragging || window.isEditing || window.isScheduling) return;
        const data = await adapter.loadData();
        people = data.people || [];
        events = data.events || [];

        // Sync schedUserData and unselectedSchedUsers directly from SQLite profiles (keyed by user_id)
        unselectedSchedUsers.clear();
        schedUserData = {};
        people.forEach(p => {
            if (p.profile) {
                schedUserData[p.id] = { ...p.profile };
                if (p.profile.is_schedulable === false) unselectedSchedUsers.add(p.id);
            }
        });

        if (visiblePeopleIndices.length === 0) {
            visiblePeopleIndices = people.map((_, i) => i);
        } else {
            visiblePeopleIndices = visiblePeopleIndices.filter(i => i < people.length);
            if (visiblePeopleIndices.length === 0) visiblePeopleIndices = people.map((_, i) => i);
        }
        populateTempFilter();
        render();
    }

    
function renderTimeline() {
        if (!gridBg || !timelineLabels || !headerRow) return;
        let activeVisible = getEffectiveVisiblePeople();
        let activeCols = isPeopleView() ? Math.max(1, activeVisible.length) : getColCount();

        let viewStart = new Date(currentDate);
        if (viewMode === 3) viewStart.setDate(viewStart.getDate() - viewStart.getDay());
        updateDateTitle(viewStart.toLocaleDateString(undefined, { month: 'long', day: 'numeric', year: 'numeric' }));

        headerRow.innerHTML = '';
        for (let i = 0; i < activeCols; i++) {
            let div = document.createElement('div');
            div.className = 'col-header';
            if (isPeopleView()) {
                let pIdx = activeVisible[i];
                div.innerText = people[pIdx] ? people[pIdx].name : '';
            } else {
                let d = new Date(viewStart); d.setDate(d.getDate() + i);
                div.innerText = d.toLocaleDateString(undefined, { weekday: 'short', month: 'numeric', day: 'numeric' });
            }
            headerRow.appendChild(div);
        }

        const totalHeight = 1440 * zoom;
        timelineLabels.style.height = `${totalHeight}px`;
        gridBg.style.height = `${totalHeight}px`;
        timelineLabels.innerHTML = '';
        for (let i = 0; i <= 24; i++) {
            let lbl = document.createElement('div');
            lbl.className = 'time-label';
            lbl.style.top = `${i * 60 * zoom}px`;
            if (i > 0 && i < 24) lbl.innerText = minToTimeStr(i * 60);
            timelineLabels.appendChild(lbl);
        }

        gridBg.innerHTML = '';
        gridBg.style.background = '';
        gridBg.style.backgroundImage = 'linear-gradient(#e0e0e0 1px, transparent 1px), linear-gradient(90deg, #dadce0 1px, transparent 1px)';
        gridBg.style.backgroundSize = `calc(100% / ${activeCols}) ${60 * zoom}px`;
        if (eventsLayer) { eventsLayer.innerHTML = ''; eventsLayer.style.pointerEvents = 'none'; }

        events.forEach(ev => {
            if (activeVisible.length > 0 && !activeVisible.includes(ev.personIdx)) return;

            let colIdx = -1;
            if (isPeopleView()) {
                if (ev.date === formatDate(currentDate)) colIdx = activeVisible.indexOf(ev.personIdx);
            } else {
                let evDate = new Date(ev.date.replace(/\//g, '-') + 'T12:00:00');
                let diffDays = Math.round((evDate.getTime() - new Date(viewStart.getFullYear(), viewStart.getMonth(), viewStart.getDate(), 12, 0, 0).getTime()) / 86400000);
                if (diffDays >= 0 && diffDays < activeCols) colIdx = diffDays;
            }

            if (colIdx >= 0 && colIdx < activeCols && eventsLayer) {
                let card = document.createElement('div');
                card.className = 'event-card' + (selectedEventId === ev.id ? ' selected' : '');
                card.style.top = `${ev.startMin * zoom}px`;
                card.style.height = `${Math.max(18, ev.duration * zoom)}px`;
                card.style.left = `${(colIdx / activeCols) * 100}%`;
                card.style.width = `calc(${100 / activeCols}% - 6px)`;
                card.style.marginLeft = '3px';
                card.style.backgroundColor = ev.color || colors[ev.personIdx % colors.length];
                card.style.pointerEvents = 'auto';
                card.style.touchAction = 'none';
                card.dataset.id = ev.id;

                let pName = people[ev.personIdx]?.name || "";
                let subLine = !isPeopleView() ? `<div class="event-time">${pName}</div>` : '';
                let footerLine = ev.footer ? `<div class="event-time">${ev.footer}</div>` : '';
                card.innerHTML = `
                    <div class="event-title">${ev.title}</div>
                    ${subLine}
                    <div class="event-time">${minToTimeStr(ev.startMin)} - ${minToTimeStr(ev.startMin + ev.duration)}</div>
                    ${footerLine}
                    <div class="resize-handle" style="touch-action:none;"></div>
                `;

                bindDoubleTap(card, (e) => {
                    e.stopPropagation();
                    if (!isEditMode()) return;
                    if (adapter.onEventEdit) {
                        adapter.onEventEdit(ev, people, refresh);
                    }
                });

                card.addEventListener('mousedown', handleDrag);
                card.addEventListener('touchstart', handleDrag, { passive: false });
                card.querySelector('.resize-handle').addEventListener('mousedown', handleResize);
                card.querySelector('.resize-handle').addEventListener('touchstart', handleResize, { passive: false });
                eventsLayer.appendChild(card);
            }
        });
    }
function renderMonth() {
        if (!gridBg || !headerRow || !timelineLabels) return;
        let activeVisible = getEffectiveVisiblePeople();
        updateDateTitle(currentDate.toLocaleDateString(undefined, { month: 'long', year: 'numeric' }));

        headerRow.innerHTML = '';
        ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"].forEach(d => {
            let div = document.createElement('div');
            div.className = 'col-header'; div.style.flex = '1'; div.innerText = d;
            headerRow.appendChild(div);
        });

        timelineLabels.innerHTML = '';
        gridBg.style.background = 'none'; gridBg.innerHTML = '';
        if (eventsLayer) eventsLayer.innerHTML = '';

        let grid = document.createElement('div'); grid.className = 'month-grid';
        let y = currentDate.getFullYear(), m = currentDate.getMonth();
        let firstDay = new Date(y, m, 1).getDay();
        let daysInMonth = new Date(y, m + 1, 0).getDate();
        let cellCount = 0;

        for (let row = 0; row < 6; row++) {
            for (let col = 0; col < 7; col++) {
                let cell = document.createElement('div'); cell.className = 'month-cell';
                let dayNum = cellCount - firstDay + 1;
                let cellDateStr, displayNum, isOther = false;
                if (dayNum <= 0) {
                    displayNum = new Date(y, m, 0).getDate() + dayNum;
                    cellDateStr = formatDate(new Date(y, m - 1, displayNum));
                    isOther = true;
                } else if (dayNum > daysInMonth) {
                    displayNum = dayNum - daysInMonth;
                    cellDateStr = formatDate(new Date(y, m + 1, displayNum));
                    isOther = true;
                } else {
                    displayNum = dayNum;
                    cellDateStr = `${y}/${String(m + 1).padStart(2, '0')}/${String(dayNum).padStart(2, '0')}`;
                }

                cell.innerHTML = `<div class="month-day-num" style="${isOther ? 'color:#9aa0a6;' : ''}">${displayNum}</div>`;
                cell.dataset.date = cellDateStr; cell.dataset.other = isOther ? 'true' : 'false';
                if (isOther) cell.style.backgroundColor = '#f1f3f4';

                events.filter(e => e.date === cellDateStr && activeVisible.includes(e.personIdx)).forEach(ev => {
                    let evDiv = document.createElement('div');
                    evDiv.className = 'month-event';
                    evDiv.style.backgroundColor = ev.color || colors[ev.personIdx % colors.length];
                    evDiv.innerText = `${ev.title} - ${people[ev.personIdx]?.name || ''}`;
                    evDiv.dataset.id = ev.id;
                    if (selectedEventId === ev.id) evDiv.style.outline = "2px solid #000";
                    evDiv.onclick = (e) => {
                        e.stopPropagation(); selectedEventId = ev.id;
                        document.querySelectorAll('.month-event').forEach(el => el.style.outline = 'none');
                        evDiv.style.outline = "2px solid #000";
                    };
                    evDiv.addEventListener('mousedown', startMonthDrag);
                    evDiv.addEventListener('touchstart', startMonthDrag, { passive: false });
                    bindDoubleTap(evDiv, (e) => {
                        e.stopPropagation();
                        if (isEditMode() && adapter.onEventEdit) adapter.onEventEdit(ev, people, refresh);
                    });
                    cell.appendChild(evDiv);
                });

                bindDoubleTap(cell, async () => {
                    if (!isEditMode() || people.length === 0) return;
                    let pIdx = activeVisible[0] ?? 0;
                    let newEv = adapter.createDefaultEvent(pIdx, people[pIdx], cellDateStr, 540, 480, colors[pIdx % colors.length]);
                    let saved = await adapter.saveEvent(newEv);
                    if (saved) { events.push(saved); render(); }
                });

                grid.appendChild(cell);
                cellCount++;
            }
        }
        gridBg.appendChild(grid);
    }

    function renderUpcoming() {
        updateDateTitle("Upcoming Shifts");
        let activeVisible = getEffectiveVisiblePeople();
        headerRow.innerHTML = ''; timelineLabels.innerHTML = '';
        gridBg.style.background = 'none'; gridBg.innerHTML = '';
        if (eventsLayer) eventsLayer.innerHTML = '';

        let container = document.createElement('div'); container.className = 'upcoming-list';
        let todayStr = formatDate(new Date());
        events.filter(e => e.date >= todayStr && activeVisible.includes(e.personIdx))
            .sort((a, b) => a.date.localeCompare(b.date) || a.startMin - b.startMin)
            .forEach(ev => {
                let card = document.createElement('div'); card.className = 'upcoming-card';
                card.innerHTML = `
                    <div class="upcoming-color" style="background:${ev.color || colors[ev.personIdx % colors.length]}"></div>
                    <div class="upcoming-details">
                        <h3>${ev.title} - ${people[ev.personIdx]?.name || ''}</h3>
                        <p>${ev.date} &nbsp;|&nbsp; ${minToTimeStr(ev.startMin)} - ${minToTimeStr(ev.startMin + ev.duration)}</p>
                    </div>`;
                container.appendChild(card);
            });
        gridBg.appendChild(container);
    }

    function renderScheduleTable() {
        let viewStart = new Date(currentDate);
        viewStart.setDate(viewStart.getDate() - viewStart.getDay());
        updateDateTitle("Weekly Schedule");
        headerRow.innerHTML = ''; timelineLabels.innerHTML = '';
        gridBg.style.background = 'none'; gridBg.innerHTML = '';
        if (eventsLayer) eventsLayer.innerHTML = '';

        let table = document.createElement('table');
        table.style.cssText = 'width:100%; border-collapse:collapse; font-size:12px; text-align:left; table-layout:fixed; user-select:text;';

        let hr = `<tr style="border-bottom:2px solid #dadce0; background:#f8f9fa;"><th style="padding:6px; width:14%;">Staff</th><th style="padding:6px; width:6%;">Hrs</th>`;
        for (let i = 0; i < 7; i++) {
            let d = new Date(viewStart); d.setDate(d.getDate() + i);
            hr += `<th style="padding:6px; border-left:1px solid #dadce0;">${d.toLocaleDateString(undefined, { weekday: 'short', month: 'numeric', day: 'numeric' })}</th>`;
        }
        hr += `</tr>`;

        let gRestMins = parseFloat(document.getElementById('globalSchedRest')?.value || '0') * 60 || 0;
        let activeVisible = getEffectiveVisiblePeople();
        let rowsHtml = '';

        activeVisible.forEach(pIdx => {
            if (!people[pIdx]) return;
            let uRestMins = (schedUserData[pIdx]?.r && schedUserData[pIdx].r.toLowerCase() !== 'clear')
                ? parseFloat(schedUserData[pIdx].r) * 60 : gRestMins;

            let totalPaidMins = 0, dayCells = '';
            for (let i = 0; i < 7; i++) {
                let d = new Date(viewStart); d.setDate(d.getDate() + i);
                let dStr = formatDate(d);
                let dayEvs = events.filter(e => e.date === dStr && e.personIdx === pIdx).sort((a, b) => a.startMin - b.startMin);

                let workMins = 0;
                dayEvs.forEach(e => { if (!isOffEvent(e)) workMins += e.duration; });
                totalPaidMins += (workMins > uRestMins) ? (workMins - uRestMins) : workMins;

                let cellHtml = dayEvs.map(e => {
                    let hrs = e.duration / 60;
                    let hrsStr = Number.isInteger(hrs) ? hrs : hrs.toFixed(1);
                    let timeStr = `${minToTimeStr(e.startMin)} - ${minToTimeStr(e.startMin + e.duration)}`;
                    return `<div style="background:${e.color || colors[pIdx % colors.length]}; color:#fff; border-radius:4px; padding:4px 6px; margin-bottom:4px; font-size:11px; line-height:1.3;">
                        <b>${e.title} (${hrsStr}h)</b>${isOffEvent(e) ? '' : `<br>${timeStr}`}
                    </div>`;
                }).join('');
                dayCells += `<td style="padding:4px; border-left:1px solid #e0e0e0; vertical-align:top;">${cellHtml}</td>`;
            }

            rowsHtml += `<tr style="border-bottom:1px solid #e0e0e0;">
                <td style="padding:6px; font-weight:bold; color:#3c4043;">${people[pIdx].name}</td>
                <td style="padding:6px; font-weight:bold; color:#3c4043;">${(totalPaidMins / 60).toFixed(1)}h</td>
                ${dayCells}
            </tr>`;
        });

        table.innerHTML = `<thead>${hr}</thead><tbody>${rowsHtml}</tbody>`;
        let container = document.createElement('div');
        container.style.cssText = 'padding:10px; overflow-y:auto; height:100%; width:100%; user-select:text;';
        container.appendChild(table);
        gridBg.appendChild(container);
    }

    function render() {
        if (viewMode === 6) renderMonth();
        else if (viewMode === 7) renderUpcoming();
        else if (viewMode === 8) renderScheduleTable();
        else renderTimeline();
    }

    /* --- DRAG, RESIZE & CREATE --- */
    function handleDrag(e) {
        if (!isEditMode() || window.isEditing) return;
        if (e.target.classList.contains('resize-handle') || e.target.tagName === 'INPUT') return;
        e.stopPropagation();

        selectedEventId = e.currentTarget.dataset.id;
        activeEv = events.find(ev => String(ev.id) === String(selectedEventId));
        if (!activeEv) return;
        if (e.type === 'touchstart' && e.cancelable) e.preventDefault();

        originalDateStr = activeEv.date;
        if (e.ctrlKey || e.metaKey) {
            let cloned = { ...activeEv, id: null, meta: { ...(activeEv.meta || {}) } };
            events.push(cloned);
            activeEv = cloned;
            selectedEventId = cloned.id;
        } else {
            document.querySelectorAll('.event-card').forEach(c => c.classList.remove('selected'));
            e.currentTarget.classList.add('selected');
        }

        originalStartMin = activeEv.startMin;
        startMouseY = getClientY(e); startMouseX = getClientX(e);
        isDragging = true; hasMoved = false;
        activeCard = e.currentTarget;
    }

    function handleResize(e) {
        if (!isEditMode() || window.isEditing) return;
        e.stopPropagation();
        activeCard = e.target.parentElement;
        selectedEventId = activeCard.dataset.id;
        activeEv = events.find(ev => String(ev.id) === String(selectedEventId));
        if (!activeEv) return;
        if (e.type === 'touchstart' && e.cancelable) e.preventDefault();
        isResizing = true; hasMoved = true;
        startMouseY = getClientY(e); originalDuration = activeEv.duration;
    }

    function startMonthDrag(e) {
        if (!isEditMode() || window.isEditing) return;
        e.stopPropagation();
        selectedEventId = this.dataset.id;
        monthDragEv = events.find(x => String(x.id) === String(selectedEventId));
        if (!monthDragEv) return;
        if (e.type === 'touchstart' && e.cancelable) e.preventDefault();
        isMonthDragging = true; hasMoved = false;
        document.querySelectorAll('.month-event').forEach(el => el.style.outline = 'none');
        this.style.outline = "2px solid #000";
    }

    function handleMove(e) {
        if (isMonthDragging && monthDragEv) {
            if (e.cancelable) e.preventDefault();
            hasMoved = true;
            let target = document.elementFromPoint(getClientX(e), getClientY(e));
            document.querySelectorAll('.month-cell').forEach(c => c.style.backgroundColor = c.dataset.other === 'true' ? '#f1f3f4' : '');
            if (target?.closest('.month-cell')) target.closest('.month-cell').style.backgroundColor = '#e8eaed';
            return;
        }
        if (!activeCard || !activeEv) return;
        if ((isDragging || isResizing) && e.cancelable) e.preventDefault();

        if (isDragging) {
            let deltaY = getClientY(e) - startMouseY, deltaX = getClientX(e) - startMouseX;
            if (!hasMoved && Math.abs(deltaY) < 5 && Math.abs(deltaX) < 5) return;
            hasMoved = true;

            let deltaMins = Math.round(deltaY / zoom / 15) * 15;
            let newMin = Math.max(0, Math.min(1440 - activeEv.duration, originalStartMin + deltaMins));
            activeEv.startMin = newMin;
            activeCard.style.top = `${newMin * zoom}px`;

            let activeVisible = getEffectiveVisiblePeople();
            let activeCols = isPeopleView() ? Math.max(1, activeVisible.length) : getColCount();
            let rect = gridBg.getBoundingClientRect();
            let newColIdx = Math.max(0, Math.min(activeCols - 1, Math.floor((getClientX(e) - rect.left) / (rect.width / activeCols))));

            if (isPeopleView()) {
                activeEv.personIdx = activeVisible[newColIdx];
                activeEv.color = colors[activeEv.personIdx % colors.length];
                activeCard.style.backgroundColor = activeEv.color;
            } else {
                let dt = new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate());
                if (viewMode === 3) dt.setDate(dt.getDate() - dt.getDay());
                dt.setDate(dt.getDate() + newColIdx);
                activeEv.date = formatDate(dt);
            }
            activeCard.style.left = `${(newColIdx / activeCols) * 100}%`;
        } else if (isResizing) {
            let deltaMins = Math.round((getClientY(e) - startMouseY) / zoom / 15) * 15;
            let newDur = Math.max(15, Math.min(1440 - activeEv.startMin, originalDuration + deltaMins));
            activeEv.duration = newDur;
            activeCard.style.height = `${newDur * zoom}px`;
        }
    }

    async function handleUp(e) {
        if (isMonthDragging && monthDragEv) {
            let evRef = monthDragEv;
            isMonthDragging = false; monthDragEv = null;
            if (hasMoved) {
                let cell = document.elementFromPoint(getClientX(e), getClientY(e))?.closest('.month-cell');
                if (cell?.dataset.date && evRef.date !== cell.dataset.date) {
                    evRef.date = cell.dataset.date;
                    await adapter.saveEvent(evRef, people[evRef.personIdx]);
                }
            }
            render();
            return;
        }
        if ((isDragging || isResizing) && activeEv && hasMoved) {
            let evRef = activeEv;
            isDragging = isResizing = false; activeCard = activeEv = null;
            let saved = await adapter.saveEvent(evRef, people[evRef.personIdx]);
            if (saved && !evRef.id) evRef.id = saved.id;
            render();
            return;
        }
        isDragging = isResizing = false; activeCard = activeEv = null;
    }

    /* --- AUTO-SCHEDULING HELPERS (from calendar.js) --- */
    function parseTimeStr(t) {
        if (!t) return NaN;
        let p = t.split(':'), h = parseInt(p[0], 10), m = p.length > 1 ? parseInt(p[1], 10) : 0;
        if (isNaN(m)) m = 0;
        if (h >= 1 && h <= 7) h += 12;
        return h * 60 + m;
    }

    function validateSchedInput(type, val) {
        if (!val || /^(clear|default)$/i.test(val)) return true;
        if (['weekly', 'daily', 'rest'].includes(type)) { let n = parseFloat(val); return !isNaN(n) && n >= 0; }
        if (['coverage', 'pref'].includes(type)) {
            for (let p of val.split(',')) {
                let t = p.trim(); if (!t) continue;
                if (t.includes('=')) t = t.split('=')[1].trim();
                let b = t.split('-'); if (b.length !== 2) return false;
                if (isNaN(parseTimeStr(b[0])) || isNaN(parseTimeStr(b[1]))) return false;
            }
            return true;
        }
        if (type === 'days') return /^([1-7](,[1-7])*)?$/.test(val.replace(/\s/g, ''));
        return true;
    }

    function populateWeeks() {
        let sel = document.getElementById('schedWeekSelect'); if (!sel) return;
        sel.innerHTML = '';
        let d = new Date(currentSchedYear, 0, 1); d.setDate(d.getDate() - d.getDay());
        let w = 1, selIdx = 0;
        let disp = new Date(currentDate); disp.setDate(disp.getDate() - disp.getDay());
        let dispWeekStr = formatDate(disp);

        while (d.getFullYear() <= currentSchedYear) {
            if (d.getFullYear() === currentSchedYear || w === 1) {
                let opt = document.createElement('option');
                let dtStr = formatDate(d); opt.value = dtStr; opt.innerText = `${dtStr} W${String(w).padStart(2, '0')}`;
                sel.appendChild(opt); if (dtStr === dispWeekStr) selIdx = sel.options.length - 1; w++;
            }
            d.setDate(d.getDate() + 7);
        }
        if (sel.options.length > selIdx) sel.selectedIndex = selIdx;
    }

    function renderSchedUsers() {
        let filter = (document.getElementById('schedUserFilter')?.value || '').toLowerCase();
        let list = document.getElementById('schedUserList'); if (!list) return;
        list.innerHTML = '';

        let usersArr = people.map((p, i) => {
            let prof = schedUserData[p.id] || {};
            return {
                pIdx: i, uid: p.id, name: p.name,
                w: prof.w || '', da: prof.da || '', r: prof.r || '',
                c: prof.c || '', p_val: prof.p || '', d: prof.d || ''
            };
        }).filter(u => u.name.toLowerCase().includes(filter));

        usersArr.sort((a, b) => {
            let valA = a.name.toLowerCase(), valB = b.name.toLowerCase();
            if (currentSortCol === 'weekly') { valA = parseFloat(a.w) || 0; valB = parseFloat(b.w) || 0; }
            else if (currentSortCol === 'daily') { valA = parseFloat(a.da) || 0; valB = parseFloat(b.da) || 0; }
            else if (currentSortCol === 'rest') { valA = parseFloat(a.r) || 0; valB = parseFloat(b.r) || 0; }
            else if (currentSortCol === 'coverage') { valA = a.c; valB = b.c; }
            else if (currentSortCol === 'pref') { valA = a.p_val; valB = b.p_val; }
            else if (currentSortCol === 'days') { valA = a.d; valB = b.d; }
            return valA < valB ? -1 * currentSortDir : (valA > valB ? 1 * currentSortDir : 0);
        });

        usersArr.forEach(u => {
            let div = document.createElement('div');
            div.style.cssText = 'display:flex; gap:10px; margin-bottom:6px; align-items:center; padding:0 10px;';
            div.dataset.uid = u.uid;
            let isChecked = !unselectedSchedUsers.has(u.uid);
            div.innerHTML = `
                <input type="checkbox" class="sched-chk" value="${u.uid}" style="width:15px; margin:0;" ${isChecked ? 'checked' : ''}>
                <label style="width:140px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;" title="${u.name}">${u.name}</label>
                <input type="text" class="sched-coverage" data-type="coverage" list="dl-coverage" style="width:120px; padding:2px;">
                <input type="text" class="sched-pref" data-type="pref" list="dl-pref" style="width:120px; padding:2px;">
                <input type="text" class="sched-days" data-type="days" list="dl-days" style="width:85px; padding:2px;">
                <input type="text" class="sched-rest" data-type="rest" list="dl-rest" style="width:75px; padding:2px;">
                <input type="text" class="sched-daily" data-type="daily" list="dl-daily" style="width:75px; padding:2px;">
                <input type="text" class="sched-weekly" data-type="weekly" list="dl-weekly" style="width:75px; padding:2px;">
            `;
            list.appendChild(div);
            div.querySelector('.sched-coverage').value = u.c;
            div.querySelector('.sched-pref').value = u.p_val;
            div.querySelector('.sched-days').value = u.d;
            div.querySelector('.sched-rest').value = u.r;
            div.querySelector('.sched-daily').value = u.da;
            div.querySelector('.sched-weekly').value = u.w;
        });
    }

    
async function runAutoSchedule() {
        let activeMode = document.getElementById('comboDefaultEvent')?.value || "WORKING_REGULAR";
        if (!confirm(`Generate automated weekly schedule using ${activeMode}?`)) return;

        window.isScheduling = true;
        let scheduledStartDate = null;
        try {
            let weekStartStr = document.getElementById('schedWeekSelect').value;
            if (!weekStartStr) {
                alert("Please select a valid week start date.");
                return;
            }
            let [y, m, d] = weekStartStr.split(/[-/]/);
            let startDate = new Date(parseInt(y, 10), parseInt(m, 10) - 1, parseInt(d, 10), 12, 0, 0);
            scheduledStartDate = startDate;

            const parseTimePart = (tStr) => {
                let match = tStr.trim().match(/^(\d{1,2})(?::(\d{2}))?\s*(am|pm)?$/i);
                if (!match) return null;
                let h = parseInt(match[1], 10), mins = match[2] ? parseInt(match[2], 10) : 0;
                let mer = match[3] ? match[3].toLowerCase() : null;
                let hasLeadingZero = match[1].length === 2 && match[1].startsWith('0');
                if (h > 24 || mins < 0 || mins >= 60) return null;
                return { h, mins, mer, hasLeadingZero };
            };

            const parseShiftList = (rawStr) => {
                if (!rawStr || /^(default|clear|none)$/i.test(rawStr.trim())) return [];
                let explicitEntries = [], defaultEntries = [], explicitDaysSet = new Set();

                for (let part of rawStr.split(',')) {
                    part = part.trim(); if (!part) continue;
                    let dayIdx = null, rangeStr = part;
                    if (part.includes('=')) {
                        let eq = part.split('='); if (eq.length !== 2) continue;
                        let dNum = parseInt(eq[0].trim(), 10);
                        if (isNaN(dNum) || dNum < 1 || dNum > 7) continue;
                        dayIdx = dNum - 1; rangeStr = eq[1].trim();
                    }
                    let dash = rangeStr.split('-'); if (dash.length !== 2) continue;
                    let sT = parseTimePart(dash[0]), eT = parseTimePart(dash[1]);
                    if (!sT || !eT) continue;

                    let sH = sT.h, eH = eT.h;
                    if (sT.mer === 'am' && sH === 12) sH = 0; else if (sT.mer === 'pm' && sH < 12) sH += 12;
                    if (eT.mer === 'am' && eH === 12) eH = 0; else if (eT.mer === 'pm' && eH < 12) eH += 12;
                    if (!sT.mer && !sT.hasLeadingZero && sH >= 1 && sH <= 5) {
                        if (!(eH > 12 && (sH + 12) * 60 + sT.mins >= eH * 60 + eT.mins)) sH += 12;
                    }
                    let startMin = sH * 60 + sT.mins, endMin = eH * 60 + eT.mins;
                    if (!eT.mer && !eT.hasLeadingZero && eH <= 12 && endMin <= startMin) { eH += 12; endMin = eH * 60 + eT.mins; }
                    let span = endMin - startMin;
                    if (span <= 0 || span > 1440) continue;

                    if (dayIdx !== null) { explicitDaysSet.add(dayIdx); explicitEntries.push({ start: startMin, span, days: [dayIdx] }); }
                    else defaultEntries.push({ start: startMin, span });
                }
                let fallbackDays = [0, 1, 2, 3, 4, 5, 6].filter(x => !explicitDaysSet.has(x));
                let res = [...explicitEntries];
                if (defaultEntries.length > 0 && fallbackDays.length > 0) {
                    defaultEntries.forEach(def => res.push({ start: def.start, span: def.span, days: fallbackDays }));
                }
                return res;
            };

            const findAvailableSlots = (rangeStartMin, rangeEndMin, offEvents) => {
                let sorted = offEvents.map(e => ({ s: Number(e.startMin), e: Number(e.startMin) + Number(e.duration) })).sort((a, b) => a.s - b.s);
                let cur = Number(rangeStartMin), end = Number(rangeEndMin), chunks = [];
                for (let o of sorted) {
                    if (o.e <= cur || o.s >= end) continue;
                    if (o.s > cur) chunks.push({ start: cur, end: Math.min(o.s, end) });
                    if (o.e > cur) cur = o.e;
                }
                if (cur < end) chunks.push({ start: cur, end });
                return chunks;
            };

            let weekDates = [];
            for (let i = 0; i < 7; i++) {
                let cur = new Date(startDate); cur.setDate(cur.getDate() + i);
                weekDates.push(formatDate(cur));
            }

            let intervalCoverage = Array(7).fill(0).map(() => Array(48).fill(0));
            for (let day = 0; day < 7; day++) {
                events.filter(e => e.date === weekDates[day] && !isOffEvent(e)).forEach(e => {
                    let sIdx = Math.max(0, Math.floor(e.startMin / 30));
                    let eIdx = Math.min(48, Math.ceil((e.startMin + e.duration) / 30));
                    for (let idx = sIdx; idx < eIdx; idx++) intervalCoverage[day][idx]++;
                });
            }

            let gWeekly = document.getElementById('globalSchedWeekly')?.value || "40";
            let gDaily = document.getElementById('globalSchedDaily')?.value || "8";
            let gRest = document.getElementById('globalSchedRest')?.value || "0.5";
            let gCoverage = document.getElementById('globalSchedCoverage')?.value || "9-5";
            let gPref = document.getElementById('globalSchedPref')?.value || "Default";
            let gDays = document.getElementById('globalSchedDays')?.value || "2,3,4,5,6";

            let users = [];
            for (let pIdx = 0; pIdx < people.length; pIdx++) {
                let uid = people[pIdx].id;
                if (unselectedSchedUsers.has(uid)) continue;

                let data = schedUserData[uid] || {};
                let pW = (!data.w || data.w.toLowerCase() === 'clear') ? gWeekly : data.w;
                let pDa = (!data.da || data.da.toLowerCase() === 'clear') ? gDaily : data.da;
                let pR = (!data.r || data.r.toLowerCase() === 'clear') ? gRest : data.r;
                let pC = (!data.c || data.c.toLowerCase() === 'clear') ? gCoverage : data.c;
                let hasCustomPref = !!(data.p && !/^(clear|default)$/i.test(data.p));
                let pP = (!data.p || data.p.toLowerCase() === 'clear') ? gPref : data.p;
                let pD = (!data.d || data.d.toLowerCase() === 'clear') ? gDays : data.d;

                let weeklyMins = Math.min(168 * 60, parseFloat(pW) * 60);
                if (isNaN(weeklyMins) || weeklyMins <= 0) continue;
                let dailyMaxMins = Math.min(16 * 60, parseFloat(pDa) * 60 || 480);
                let restMins = Math.min(180, Math.max(0, parseFloat(pR) * 60 || 0));
                let allowedDays = pD.split(',').map(n => parseInt(n.trim(), 10) - 1).filter(n => !isNaN(n) && n >= 0 && n <= 6);
                if (allowedDays.length === 0) allowedDays = [1, 2, 3, 4, 5];

                let templates = [
                    ...parseShiftList(pP).map(s => ({ ...s, isPref: hasCustomPref })),
                    ...parseShiftList(pC).map(s => ({ ...s, isPref: false }))
                ];
                if (templates.length === 0) continue;

                let dailyMins = Array(7).fill(0), preTotal = 0;
                for (let i = 0; i < 7; i++) {
                    let dTotal = events.filter(e => e.date === weekDates[i] && e.personIdx === pIdx && !isOffEvent(e))
                        .reduce((sum, e) => sum + Number(e.duration), 0);
                    dailyMins[i] = dTotal; preTotal += dTotal;
                }
                let remWeekly = Math.max(0, weeklyMins - preTotal);
                if (remWeekly < 60) continue;

                users.push({ pIdx, uid, weeklyMins: remWeekly, restMins, dailyMaxMins, templates, allowedDays, dailyMins });
            }

            if (users.length === 0) {
                alert("No active staff members selected or configured for scheduling.");
                return;
            }

            let generatedEvents = [];
            let maxIterations = users.length * 7 + 10;

            for (let phase = 1; phase <= 2; phase++) {
                let madeAssignment = true, iter = 0;
                while (madeAssignment && iter++ < maxIterations) {
                    madeAssignment = false;
                    let bestScore = Infinity, bestUser = null, bestDay = -1, bestShift = null;

                    for (let u of users) {
                        if (u.weeklyMins < 60) continue;
                        for (let day = 0; day < 7; day++) {
                            if (u.dailyMins[day] > 0) continue;
                            let curDate = new Date(startDate); curDate.setDate(curDate.getDate() + day);
                            let dow = curDate.getDay();
                            if (!u.allowedDays.includes(dow)) continue;

                            let dStr = weekDates[day];
                            let dayOffs = events.filter(e => e.date === dStr && e.personIdx === u.pIdx && isOffEvent(e));

                            for (let t of u.templates) {
                                if (phase === 1 && !t.isPref) continue;
                                if (t.days && !t.days.includes(dow)) continue;

                                let targetPaid = Math.min(u.weeklyMins, u.dailyMaxMins, t.span);
                                let actualSpan = Math.min(t.span, targetPaid + u.restMins, 1440 - t.start);
                                if (actualSpan < 60) continue;
                                let actualPaid = actualSpan > u.restMins ? actualSpan - u.restMins : actualSpan;

                                for (let slide = 0; slide <= t.span - actualSpan; slide += 30) {
                                    let sStart = t.start + slide;
                                    if (sStart + actualSpan > 1440) continue;

                                    let offOverlap = 0;
                                    for (let off of dayOffs) {
                                        let ovS = Math.max(sStart, off.startMin), ovE = Math.min(sStart + actualSpan, off.startMin + off.duration);
                                        if (ovS < ovE) offOverlap += (ovE - ovS);
                                    }
                                    let expectedYield = actualPaid - offOverlap;
                                    if (expectedYield < 60) continue;

                                    let penalty = 0;
                                    let sIdx = Math.floor(sStart / 30), eIdx = Math.min(48, Math.ceil((sStart + actualSpan) / 30));
                                    for (let idx = sIdx; idx < eIdx; idx++) {
                                        let c = intervalCoverage[day][idx];
                                        penalty -= (c === 0 ? 10000 : c === 1 ? 1000 : c === 2 ? 100 : 10);
                                    }
                                    if (t.isPref) penalty -= 500;
                                    let score = penalty - (expectedYield * 1000) - (u.weeklyMins * 100);
                                    if (score < bestScore) {
                                        bestScore = score; bestUser = u; bestDay = day;
                                        bestShift = { start: sStart, span: actualSpan, paid: expectedYield };
                                    }
                                }
                            }
                        }
                    }

                    if (bestUser && bestShift) {
                        let dStr = weekDates[bestDay];
                        let dayOffs = events.filter(e => e.date === dStr && e.personIdx === bestUser.pIdx && isOffEvent(e));
                        let chunks = dayOffs.length > 0
                            ? findAvailableSlots(bestShift.start, bestShift.start + bestShift.span, dayOffs)
                            : [{ start: bestShift.start, end: bestShift.start + bestShift.span }];

                        chunks.forEach(ch => {
                            if (ch.end - ch.start >= 30) {
                                generatedEvents.push(adapter.createDefaultEvent(
                                    bestUser.pIdx, people[bestUser.pIdx], dStr, ch.start, ch.end - ch.start, colors[bestUser.pIdx % colors.length]
                                ));
                            }
                        });

                        let sIdx = Math.floor(bestShift.start / 30), eIdx = Math.min(48, Math.ceil((bestShift.start + bestShift.span) / 30));
                        for (let idx = sIdx; idx < eIdx; idx++) intervalCoverage[bestDay][idx]++;
                        bestUser.weeklyMins = Math.max(0, bestUser.weeklyMins - bestShift.paid);
                        bestUser.dailyMins[bestDay] += bestShift.paid;
                        madeAssignment = true;
                    }
                }
            }

            if (generatedEvents.length > 0) {
                await adapter.saveBatch(generatedEvents, []);
            } else {
                alert("Auto-scheduler completed but could not generate any shifts matching the active preferences or coverage hours.");
            }
            document.getElementById('schedulingModal').style.display = 'none';
            if (scheduledStartDate) currentDate = new Date(scheduledStartDate);
        } catch (err) {
            console.error("Auto-schedule error:", err);
            alert("Auto-schedule failed: " + err.message);
        } finally {
            window.isScheduling = false;
        }
        await refresh();
    }

    
async function clearWeekSchedule() {
        let activeMode = document.getElementById('comboDefaultEvent')?.value || "WORKING_REGULAR";
        if (!confirm(`Clear all ${activeMode} shifts for the selected week?`)) return;
        window.isScheduling = true;
        try {
            let weekStartStr = document.getElementById('schedWeekSelect').value;
            let [y, m, d] = weekStartStr.split(/[-/]/);
            let startDate = new Date(parseInt(y, 10), parseInt(m, 10) - 1, parseInt(d, 10), 12, 0, 0);
            let weekDates = new Set();
            for (let i = 0; i < 7; i++) { let cur = new Date(startDate); cur.setDate(cur.getDate() + i); weekDates.add(formatDate(cur)); }

            let toDelete = events.filter(e =>
                weekDates.has(e.date) &&
                ((e.meta?.shift_type || e.title || "").toUpperCase().includes(activeMode.toUpperCase()))
            );
            if (toDelete.length > 0) {
                await adapter.saveBatch([], toDelete.map(e => e.id));
            }
        } finally {
            window.isScheduling = false;
        }
        await refresh();
    }
/* --- EXPORT TXT & PDF --- */
    function exportTXT() {
        let activeVisible = getEffectiveVisiblePeople();
        let viewStart = new Date(currentDate); viewStart.setDate(viewStart.getDate() - viewStart.getDay());
        let pad = (str, len) => (str.length > len ? str.substring(0, len) : str.padEnd(len, ' '));
        let content = "STAFF            | HRS  | SUN            | MON            | TUE            | WED            | THU            | FRI            | SAT\r\n" + "=".repeat(140) + "\r\n";

        activeVisible.forEach(pIdx => {
            if (!people[pIdx]) return;
            let totalMins = 0, cols = [];
            for (let i = 0; i < 7; i++) {
                let d = new Date(viewStart); d.setDate(d.getDate() + i);
                let dayEvs = events.filter(e => e.date === formatDate(d) && e.personIdx === pIdx).sort((a, b) => a.startMin - b.startMin);
                dayEvs.forEach(e => { if (!isOffEvent(e)) totalMins += e.duration; });
                cols.push(dayEvs.map(e => isOffEvent(e) ? e.title : `${minToTimeStr(e.startMin)}-${minToTimeStr(e.startMin + e.duration)}`).join(', '));
            }
            content += `${pad(people[pIdx].name, 16)} | ${pad((totalMins / 60).toFixed(1) + 'h', 4)} | ${cols.map(c => pad(c, 14)).join(' | ')}\r\n`;
        });
        let a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([content], { type: 'text/plain' }));
        a.download = `Schedule_${formatDate(viewStart).replace(/\//g, '')}.txt`;
        a.click();
    }

    function exportPDF() {
        if (!window.jspdf) return alert("jsPDF library not loaded (place jspdf.umd.min.js in server folder).");
        const { jsPDF } = window.jspdf;
        const doc = new jsPDF({ orientation: 'landscape', unit: 'pt', format: 'letter' });
        let viewStart = new Date(currentDate); viewStart.setDate(viewStart.getDate() - viewStart.getDay());
        doc.setFont("helvetica", "bold"); doc.setFontSize(14);
        doc.text(`Weekly Staff Schedule - Week of ${formatDate(viewStart)}`, 24, 30);

        let y = 55;
        let headers = ["Staff", "Hrs", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
        let xPos = [24, 130, 170, 255, 340, 425, 510, 595, 680];
        doc.setFontSize(9);
        headers.forEach((h, i) => doc.text(h, xPos[i], y));
        y += 14;

        getEffectiveVisiblePeople().forEach(pIdx => {
            if (!people[pIdx]) return;
            if (y > 560) { doc.addPage(); y = 40; }
            let totalMins = 0, dayLines = [];
            for (let i = 0; i < 7; i++) {
                let d = new Date(viewStart); d.setDate(d.getDate() + i);
                let dayEvs = events.filter(e => e.date === formatDate(d) && e.personIdx === pIdx).sort((a, b) => a.startMin - b.startMin);
                dayEvs.forEach(e => { if (!isOffEvent(e)) totalMins += e.duration; });
                dayLines.push(dayEvs.map(e => isOffEvent(e) ? e.title : `${minToTimeStr(e.startMin)}-${minToTimeStr(e.startMin + e.duration)}`));
            }
            let maxL = Math.max(1, ...dayLines.map(l => l.length));
            doc.setFont("helvetica", "bold");
            doc.text(people[pIdx].name.substring(0, 18), xPos[0], y);
            doc.text(`${(totalMins / 60).toFixed(1)}h`, xPos[1], y);
            doc.setFont("helvetica", "normal");
            dayLines.forEach((lines, i) => lines.forEach((ln, j) => doc.text(ln, xPos[i + 2], y + j * 11)));
            y += maxL * 11 + 8;
        });
        doc.save(`Schedule_${formatDate(viewStart).replace(/\//g, '')}.pdf`);
    }

    // At top of CalendarEngine IIFE alongside other state vars:
    let windowListenersBound = false;

    // Replace the top section of init(userAdapter):
    async function init(userAdapter) {
        adapter = userAdapter;
        visiblePeopleIndices = [];
        selectedEventId = null;
        viewMode = adapter.defaultViewMode || 3;

        scrollArea = document.getElementById('scroll-area');
        headerRow = document.getElementById('header-row');
        timelineLabels = document.getElementById('timeline-labels');
        gridBg = document.getElementById('grid-bg');
        eventsLayer = document.getElementById('events-layer');
        lblDateTitle = document.getElementById('lblDateTitle');

        if (adapter.loadGlobalSettings) {
            const savedGlobals = await adapter.loadGlobalSettings();
            ['globalSchedWeekly', 'globalSchedDaily', 'globalSchedRest', 'globalSchedCoverage', 'globalSchedPref', 'globalSchedDays'].forEach(id => {
                let el = document.getElementById(id);
                if (!el) return;
                el.value = savedGlobals[id] !== undefined ? savedGlobals[id] : (el.dataset.default || '');
                el.addEventListener('change', (e) => {
                    if (!validateSchedInput(e.target.dataset.type, e.target.value)) { alert(`Invalid format for ${e.target.dataset.type}`); e.target.value = ''; return; }
                    adapter.saveGlobalSetting(id, e.target.value);
                });
            });
        }

        // Header & Toolbar controls
        document.getElementById('btnMenu')?.addEventListener('click', () => {
            const tb = document.getElementById('toolbar');
            if (tb) { tb.dataset.open = tb.dataset.open === 'true' ? 'false' : 'true'; tb.style.display = tb.dataset.open === 'true' ? 'flex' : 'none'; }
        });
        document.getElementById('comboView')?.addEventListener('change', e => { viewMode = parseInt(e.target.value, 10); render(); });
        document.getElementById('comboTempPersonFilter')?.addEventListener('change', render);
        document.getElementById('btnZoomIn')?.addEventListener('click', () => { zoom = Math.min(2.5, zoom + 0.2); render(); });
        document.getElementById('btnZoomOut')?.addEventListener('click', () => { zoom = Math.max(0.6, zoom - 0.2); render(); });
        document.getElementById('btnPrev')?.addEventListener('click', () => { viewMode === 6 ? currentDate.setMonth(currentDate.getMonth() - 1) : currentDate.setDate(currentDate.getDate() - (viewMode === 1 ? 1 : viewMode === 2 ? 4 : 7)); render(); });
        document.getElementById('btnNext')?.addEventListener('click', () => { viewMode === 6 ? currentDate.setMonth(currentDate.getMonth() + 1) : currentDate.setDate(currentDate.getDate() + (viewMode === 1 ? 1 : viewMode === 2 ? 4 : 7)); render(); });
        document.getElementById('btnPrevDay')?.addEventListener('click', () => { currentDate.setDate(currentDate.getDate() - 1); render(); });
        document.getElementById('btnNextDay')?.addEventListener('click', () => { currentDate.setDate(currentDate.getDate() + 1); render(); });
        document.getElementById('btnExport')?.addEventListener('click', exportTXT);
        document.getElementById('btnPrint')?.addEventListener('click', exportPDF);

        document.getElementById('btnCopy')?.addEventListener('click', async () => {
            if (!isEditMode() || !selectedEventId) return;
            let ev = events.find(x => String(x.id) === String(selectedEventId));
            if (!ev) return;
            let clone = { ...ev, id: null, meta: { ...(ev.meta || {}) } };
            let saved = await adapter.saveEvent(clone, people[clone.personIdx]);
            if (saved) { events.push(saved); selectedEventId = saved.id; render(); }
        });

        document.getElementById('btnDelete')?.addEventListener('click', async () => {
            if (!isEditMode() || !selectedEventId) return;
            let ev = events.find(x => String(x.id) === String(selectedEventId));
            if (!ev) return;
            await adapter.deleteEvent(ev);
            events = events.filter(x => String(x.id) !== String(selectedEventId));
            selectedEventId = null; render();
        });

        // Filter People modal
        document.getElementById('btnFilterPeople')?.addEventListener('click', () => {
            const list = document.getElementById('peopleFilterList'); list.innerHTML = '';
            people.forEach((p, idx) => {
                list.innerHTML += `<div><input type="checkbox" value="${idx}" id="chk_p_${idx}" ${visiblePeopleIndices.includes(idx) ? 'checked' : ''}><label for="chk_p_${idx}">${p.name}</label></div>`;
            });
            document.getElementById('peopleFilterModal').style.display = 'block';
        });
        document.getElementById('btnSavePeopleFilter')?.addEventListener('click', () => {
            visiblePeopleIndices = Array.from(document.querySelectorAll('#peopleFilterList input:checked')).map(c => parseInt(c.value, 10));
            document.getElementById('peopleFilterModal').style.display = 'none';
            populateTempFilter(); render();
        });

        // Clone for Users modal
        document.getElementById('btnCloneForUsers')?.addEventListener('click', () => {
            if (!isEditMode() || !selectedEventId) return alert("Enable Edit Mode and select a shift first.");
            const list = document.getElementById('cloneUsersList'); list.innerHTML = '';
            people.forEach((p, idx) => {
                list.innerHTML += `<div><input type="checkbox" value="${idx}" id="chk_cl_${idx}"><label for="chk_cl_${idx}">${p.name}</label></div>`;
            });
            document.getElementById('cloneUsersModal').style.display = 'block';
        });
        document.getElementById('btnDoCloneUsers')?.addEventListener('click', async () => {
            let ev = events.find(x => String(x.id) === String(selectedEventId)); if (!ev) return;
            let targets = Array.from(document.querySelectorAll('#cloneUsersList input:checked')).map(c => parseInt(c.value, 10));
            let batch = targets.map(pIdx => ({ ...ev, id: null, personIdx: pIdx, color: colors[pIdx % colors.length], meta: { ...(ev.meta || {}) } }));
            if (batch.length > 0) await adapter.saveBatch(batch, []);
            document.getElementById('cloneUsersModal').style.display = 'none';
            await refresh();
        });

        // Automated Weekly Scheduling modal
        let schedHeader = document.getElementById('schedHeaderRow');
        if (schedHeader) {
            schedHeader.innerHTML = `
                <div style="width:15px;"></div>
                <div style="width:140px;" data-sort="user">Staff</div>
                <div style="width:120px;" data-sort="coverage">Coverage Hours</div>
                <div style="width:120px;" data-sort="pref">Preferred</div>
                <div style="width:85px;" data-sort="days">Days</div>
                <div style="width:75px;" data-sort="rest">Rest(h)</div>
                <div style="width:75px;" data-sort="daily">Daily</div>
                <div style="width:75px;" data-sort="weekly">Weekly</div>`;
            schedHeader.addEventListener('click', (e) => {
                let col = e.target.dataset.sort; if (!col) return;
                if (currentSortCol === col) currentSortDir *= -1; else { currentSortCol = col; currentSortDir = 1; }
                renderSchedUsers();
            });
        }

        document.getElementById('btnScheduling')?.addEventListener('click', () => {
            populateWeeks();
            renderSchedUsers();
            document.getElementById('schedulingModal').style.display = 'flex';
        });
        document.getElementById('btnPrevWeek')?.addEventListener('click', () => {
            let s = document.getElementById('schedWeekSelect');
            if (s.selectedIndex > 0) s.selectedIndex--;
            else { currentSchedYear--; populateWeeks(); s.selectedIndex = s.options.length - 1; }
        });
        document.getElementById('btnNextWeek')?.addEventListener('click', () => {
            let s = document.getElementById('schedWeekSelect');
            if (s.selectedIndex < s.options.length - 1) s.selectedIndex++;
            else { currentSchedYear++; populateWeeks(); s.selectedIndex = 0; }
        });
        document.getElementById('schedUserFilter')?.addEventListener('input', renderSchedUsers);

        document.getElementById('btnSchedSelectAll')?.addEventListener('click', async () => {
            unselectedSchedUsers.clear();
            for (const p of people) {
                if (!schedUserData[p.id]) schedUserData[p.id] = {};
                schedUserData[p.id].is_schedulable = true;
                await adapter.saveUserProfile(p.id, schedUserData[p.id]);
            }
            renderSchedUsers();
        });
        document.getElementById('btnSchedSelectNone')?.addEventListener('click', async () => {
            for (const p of people) {
                unselectedSchedUsers.add(p.id);
                if (!schedUserData[p.id]) schedUserData[p.id] = {};
                schedUserData[p.id].is_schedulable = false;
                await adapter.saveUserProfile(p.id, schedUserData[p.id]);
            }
            renderSchedUsers();
        });
        document.getElementById('schedUserList')?.addEventListener('change', async (e) => {
            let row = e.target.closest('div[data-uid]'); if (!row) return;
            let uid = parseInt(row.dataset.uid, 10);
            if (!schedUserData[uid]) schedUserData[uid] = { is_schedulable: !unselectedSchedUsers.has(uid) };

            if (e.target.classList.contains('sched-chk')) {
                if (e.target.checked) unselectedSchedUsers.delete(uid);
                else unselectedSchedUsers.add(uid);
                schedUserData[uid].is_schedulable = e.target.checked;
                await adapter.saveUserProfile(uid, schedUserData[uid]);
                return;
            }
            if (e.target.dataset.type) {
                if (!validateSchedInput(e.target.dataset.type, e.target.value)) { alert(`Invalid format for ${e.target.dataset.type}`); e.target.value = ''; return; }
                const map = { weekly: 'w', daily: 'da', rest: 'r', coverage: 'c', pref: 'p', days: 'd' };
                schedUserData[uid][map[e.target.dataset.type]] = e.target.value;
                await adapter.saveUserProfile(uid, schedUserData[uid]);
            }
        });

        document.getElementById('btnMakeSchedule')?.addEventListener('click', runAutoSchedule);
        document.getElementById('btnSchedClearWeek')?.addEventListener('click', clearWeekSchedule);

        // Double-click grid to create shift
        bindDoubleTap(gridBg, async (e) => {
            if (viewMode >= 6 || !isEditMode() || people.length === 0) return;
            let rect = gridBg.getBoundingClientRect();
            let stMin = Math.max(0, Math.min(1440 - 60, Math.floor((getClientY(e) - rect.top) / zoom / 30) * 30));
            let activeVisible = getEffectiveVisiblePeople();
            let activeCols = isPeopleView() ? Math.max(1, activeVisible.length) : getColCount();
            let colIdx = Math.max(0, Math.min(activeCols - 1, Math.floor((getClientX(e) - rect.left) / (rect.width / activeCols))));
            let dt = new Date(currentDate.getFullYear(), currentDate.getMonth(), currentDate.getDate());
            if (viewMode === 3) dt.setDate(dt.getDate() - dt.getDay());
            if (!isPeopleView()) dt.setDate(dt.getDate() + colIdx);
            let pIdx = isPeopleView() ? activeVisible[colIdx] : activeVisible[0];

            let newEv = adapter.createDefaultEvent(pIdx, people[pIdx], formatDate(dt), stMin, 480, colors[pIdx % colors.length]);
            let saved = await adapter.saveEvent(newEv, people[pIdx]);
            if (saved) { events.push(saved); render(); }
        });

        // Replace the window.addEventListener block at the bottom of init(userAdapter):
        if (!windowListenersBound) {
            window.addEventListener('mousemove', handleMove, { passive: false });
            window.addEventListener('touchmove', handleMove, { passive: false });
            window.addEventListener('mouseup', handleUp);
            window.addEventListener('touchend', handleUp);
            window.addEventListener('resize', () => { if (!window.isEditing) render(); });
            window.addEventListener('keydown', (e) => {
                if (e.key === 'Delete' && selectedEventId && !window.isEditing && !['INPUT', 'SELECT'].includes(document.activeElement.tagName)) {
                    document.getElementById('btnDelete')?.click();
                }
            });
            windowListenersBound = true;
        }

        await refresh();
    }
return { init, refresh, render, getPeople: () => people };
})();