export default function LoginPage({
  searchParams,
}: {
  searchParams?: { error?: string; next?: string };
}) {
  const error = searchParams?.error;
  const next = searchParams?.next || "/";

  return (
    <main
      style={{
        minHeight: "100vh",
        display: "grid",
        placeItems: "center",
        padding: "2rem",
      }}
    >
      <form
        action="/api/login"
        method="post"
        style={{
          width: "100%",
          maxWidth: 320,
          display: "flex",
          flexDirection: "column",
          gap: "0.75rem",
        }}
      >
        <h1
          style={{
            fontSize: 16,
            color: "#666",
            letterSpacing: 2,
            margin: "0 0 0.5rem",
            textAlign: "center",
          }}
        >
          CTRL WEIGHT
        </h1>

        <input type="hidden" name="next" value={next} />
        <input
          name="username"
          placeholder="username"
          autoComplete="username"
          autoFocus
          style={field}
        />
        <input
          name="password"
          type="password"
          placeholder="password"
          autoComplete="current-password"
          style={field}
        />

        {error && (
          <div style={{ color: "#ffb4b4", fontSize: 13, textAlign: "center" }}>
            wrong username or password
          </div>
        )}

        <button type="submit" style={submit}>
          SIGN IN
        </button>
      </form>
    </main>
  );
}

const field: React.CSSProperties = {
  background: "#161616",
  color: "#eee",
  border: "1px solid #2a2a2a",
  borderRadius: 8,
  padding: "0.7rem 0.9rem",
  fontSize: 14,
};

const submit: React.CSSProperties = {
  background: "#243846",
  color: "#7adfff",
  border: "1px solid #3a6178",
  borderRadius: 8,
  padding: "0.7rem 0.9rem",
  fontSize: 13,
  letterSpacing: 1,
  fontWeight: 600,
  cursor: "pointer",
  marginTop: "0.25rem",
};
