import { NextRequest, NextResponse } from "next/server";
import { AUTH_COOKIE, authToken, checkCredentials } from "../../../lib/auth";

export async function POST(req: NextRequest) {
  const form = await req.formData();
  const username = String(form.get("username") || "");
  const password = String(form.get("password") || "");
  const nextRaw = String(form.get("next") || "/");
  const next = nextRaw.startsWith("/") ? nextRaw : "/";

  const url = req.nextUrl.clone();

  if (!checkCredentials(username, password)) {
    url.pathname = "/login";
    url.search = `?error=1&next=${encodeURIComponent(next)}`;
    return NextResponse.redirect(url, { status: 303 });
  }

  url.pathname = next;
  url.search = "";
  const res = NextResponse.redirect(url, { status: 303 });
  res.cookies.set(AUTH_COOKIE, await authToken(), {
    httpOnly: true,
    sameSite: "lax",
    secure: req.nextUrl.protocol === "https:",
    path: "/",
    maxAge: 60 * 60 * 24 * 30, // 30 days
  });
  return res;
}
