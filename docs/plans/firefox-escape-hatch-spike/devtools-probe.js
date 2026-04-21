// NexusKey Firefox emoji-surrogate DOM-insert spike (ASCII-safe)
// ---------------------------------------------------------------
// HOW TO USE:
//   1. Navigate to the real site where the bug happens
//   2. Click into the textarea / input / contenteditable
//   3. Use the SITE'S OWN emoji picker to insert one emoji
//   4. Do NOT press any arrow key - caret stays right after the emoji
//   5. Open DevTools (F12) -> Console
//   6. Paste this whole block and press Enter
//   7. Follow the printed instructions
(function () {
  var sel = getSelection();
  var active = document.activeElement;
  var api;

  if (active && (active.tagName === 'TEXTAREA' || active.tagName === 'INPUT')) {
    api = {
      kind: active.tagName.toLowerCase(),
      el: active,
      getText: function () { return active.value; },
      getCaret: function () { return active.selectionEnd; },
      focus: function () { active.focus(); }
    };
  } else if (sel && sel.rangeCount > 0 && sel.anchorNode) {
    var node = sel.anchorNode;
    var host = (node.nodeType === Node.TEXT_NODE ? node.parentElement : node);
    api = {
      kind: 'contenteditable',
      el: host,
      node: node,
      getText: function () {
        return (node.nodeType === Node.TEXT_NODE ? node.nodeValue : node.textContent) || '';
      },
      getCaret: function () { return sel.anchorOffset; },
      focus: function () { try { host.focus(); } catch (e) {} }
    };
  } else {
    console.error('[spike] FAIL: no editable target. Click into the field, insert emoji via site picker, then re-run.');
    return;
  }

  function cu(s) {
    var a = [];
    for (var i = 0; i < s.length; i++) {
      var h = s.charCodeAt(i).toString(16);
      while (h.length < 4) h = '0' + h;
      a.push(h);
    }
    return a.join(' ');
  }

  function dumpElement(el) {
    try {
      return el.tagName + (el.id ? '#' + el.id : '') +
        (el.className && typeof el.className === 'string' ? '.' + el.className.split(' ').join('.') : '');
    } catch (e) { return '<?>'; }
  }

  var initial = api.getText();
  var caret = api.getCaret();
  var prevCU = initial.charCodeAt(caret - 1);
  var isHS = prevCU >= 0xD800 && prevCU <= 0xDBFF;
  var prevHex = prevCU.toString(16);
  while (prevHex.length < 4) prevHex = '0' + prevHex;

  console.log('========== [NexusKey spike] ==========');
  console.log('  host       :', location.host);
  console.log('  target kind:', api.kind);
  console.log('  target el  :', dumpElement(api.el));
  console.log('  el object  :', api.el);
  console.log('  el innerHTML:', api.el.innerHTML || '(n/a)');
  console.log('  text       :', JSON.stringify(initial));
  console.log('  code units :', cu(initial));
  console.log('  caret@     :', caret);
  console.log('  char[caret-1]: U+' + prevHex.toUpperCase() + ' high-surrogate: ' + (isHS ? 'YES' : 'NO'));
  if (!isHS) {
    console.warn('  WARN: caret NOT right after a high surrogate - bug will not trigger. Fix caret position and re-run.');
  }

  window.__ns = {
    info: api,
    probe1: function () {
      console.log('---- PROBE 1: document.execCommand("insertText", false, "d-stroke") ----');
      api.focus();
      var ret;
      try { ret = document.execCommand('insertText', false, 'đ'); }
      catch (e) { console.error('  threw:', e); return; }
      var after = api.getText();
      console.log('  returned :', ret);
      console.log('  text     :', JSON.stringify(after));
      console.log('  cu       :', cu(after));
      console.log('  GOOD cu  : ... d83d de2d 0111 ...  (d-stroke AFTER emoji)');
      console.log('  BUG  cu  : ... d83d 0111 de2d ...  (d-stroke INSIDE emoji)');
      console.log('  delta len:', after.length - initial.length, '(expect 1)');
      if (after === initial) console.log('  RESULT: NOT INSERTED - site/editor rejected execCommand');
      else console.log('  RESULT: inspect cu above to decide GREEN vs RED');
      console.log('  (press Ctrl+Z in the page to undo before next probe)');
    },
    probe2: function () {
      console.log('---- PROBE 2: synthetic InputEvent (isTrusted=false) ----');
      api.focus();
      var before = api.getText();
      var ev1 = new InputEvent('beforeinput', {
        inputType: 'insertText', data: 'đ', bubbles: true, cancelable: true
      });
      console.log('  ev1.isTrusted:', ev1.isTrusted);
      api.el.dispatchEvent(ev1);
      api.el.dispatchEvent(new InputEvent('input', {
        inputType: 'insertText', data: 'đ', bubbles: true
      }));
      var after = api.getText();
      console.log('  text     :', JSON.stringify(after));
      console.log('  cu       :', cu(after));
      console.log('  delta len:', after.length - before.length);
      if (after === before) console.log('  RESULT: DOM UNCHANGED - Gecko/site ignored synthetic InputEvent');
      else console.log('  RESULT: DOM mutated - site acted on synthetic InputEvent');
      console.log('  (Ctrl+Z to undo)');
    },
    probe3: function () {
      console.log('---- PROBE 3: setRangeText (textarea/input only) ----');
      if (api.kind !== 'textarea' && api.kind !== 'input') {
        console.log('  skipped - only applies to textarea/input');
        return;
      }
      api.focus();
      var el = api.el;
      var start = el.selectionStart, end = el.selectionEnd;
      el.setRangeText('đ', start, end, 'end');
      console.log('  text :', JSON.stringify(api.getText()));
      console.log('  cu   :', cu(api.getText()));
    }
  };

  console.log('---- probes ready ----');
  console.log('  __ns.probe1()   -> execCommand (most likely to work)');
  console.log('  __ns.probe2()   -> synthetic InputEvent (likely rejected)');
  console.log('  __ns.probe3()   -> setRangeText (textarea/input only)');
  console.log('After each probe: inspect the cu line. Ctrl+Z in page to undo.');
})();
