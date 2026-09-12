document.addEventListener('DOMContentLoaded', function () {
  if (typeof renderMathInElement !== 'function') {
    document.body.classList.add('math-offline');
    return;
  }
  renderMathInElement(document.body, {
    delimiters: [
      { left: '\\[', right: '\\]', display: true },
      { left: '\\(', right: '\\)', display: false }
    ],
    throwOnError: false
  });
});

document.addEventListener('DOMContentLoaded', function () {
  var btn = document.querySelector('.reveal-all');
  if (!btn) return;
  btn.addEventListener('click', function () {
    var all = document.querySelectorAll('details.reveal');
    var open = btn.getAttribute('data-open') !== 'true';
    all.forEach(function (d) { d.open = open; });
    btn.setAttribute('data-open', open ? 'true' : 'false');
    btn.textContent = open ? 'Hide all answers' : 'Show all answers';
  });
});
