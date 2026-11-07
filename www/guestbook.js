// Livre d'or : envoie et affiche les messages via l'API JSON, sans recharger la page.
// Sans JavaScript, le formulaire classique (POST /guestbook) fonctionne toujours.

const form = document.querySelector("#guestbook-form");
const list = document.querySelector("#messages");
const error = document.querySelector("#error");

// même structure HTML que append_message_html() dans src/guestbook.c
function render(messages) {
    list.replaceChildren();

    if (messages.length === 0) {
        const empty = document.createElement("p");
        empty.textContent = "No messages yet. Be the first!";
        list.append(empty);
        return;
    }

    for (const m of messages) {
        const name = document.createElement("strong");
        name.textContent = m.name;          // textContent : jamais interprété comme du HTML

        const meta = document.createElement("p");
        meta.className = "meta";
        meta.append(name, " · " + m.date);

        const text = document.createElement("p");
        text.className = "text";
        text.textContent = m.message;

        const article = document.createElement("article");
        article.append(meta, text);
        list.append(article);
    }
}

async function refresh() {
    const res = await fetch("/api/messages");
    if (res.ok) {
        render(await res.json());
    }
}

form.addEventListener("submit", async (event) => {
    event.preventDefault();                 // annule l'envoi classique (et le rechargement)
    error.textContent = "";

    const button = form.querySelector("button");
    button.disabled = true;

    try {
        const res = await fetch("/api/messages", {
            method: "POST",
            body: new URLSearchParams(new FormData(form)),   // "name=...&message=..."
        });

        if (res.ok) {
            form.elements.message.value = "";
            await refresh();
        } else {
            const data = await res.json().catch(() => ({}));
            error.textContent = data.error || `error ${res.status}`;
        }
    } catch {
        error.textContent = "server unreachable";
    } finally {
        button.disabled = false;
    }
});
