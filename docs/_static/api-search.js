document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("[data-api-search]").forEach((input) => {
    const content = document.getElementById(input.dataset.apiSearch);
    const status = input.closest(".api-search")?.querySelector(".api-search-status");
    if (!content) return;

    const items = Array.from(content.children).filter((element) =>
      element.matches("section, .section, dl, .breathe-sectiondef")
    );

    const filter = () => {
      const query = input.value.trim().toLocaleLowerCase();
      let shown = 0;
      items.forEach((item) => {
        const matches = !query || item.textContent.toLocaleLowerCase().includes(query);
        item.classList.toggle("api-search-hidden", !matches);
        if (matches) shown += 1;
      });
      if (status) {
        status.textContent = query ? `${shown} matching section${shown === 1 ? "" : "s"}` : "";
      }
    };

    input.addEventListener("input", filter);
  });
});
