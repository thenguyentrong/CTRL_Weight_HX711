import { NextRequest, NextResponse } from "next/server";
import { AUTH_COOKIE, authEnabled, authToken } from "./lib/auth";

export async function middleware(req: NextRequest) {
  // If the gate isn't configured, don't lock anyone out.
  if (!authEnabled()) return NextResponse.next();

  const cookie = req.cookies.get(AUTH_COOKIE)?.value;
  const expected = await authToken();
  if (cookie && cookie === expected) return NextResponse.next();

  const url = req.nextUrl.clone();
  url.pathname = "/login";
  url.search = `?next=${encodeURIComponent(req.nextUrl.pathname)}`;
  return NextResponse.redirect(url);
}

// Gate everything except the login page, the auth API routes, and static assets.
export const config = {
  matcher: ["/((?!login|api/login|api/logout|_next/static|_next/image|favicon.ico).*)"],
};
