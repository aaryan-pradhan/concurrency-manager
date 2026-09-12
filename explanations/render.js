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
