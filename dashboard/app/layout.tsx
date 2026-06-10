import type { ReactNode } from "react";

export const metadata = {
  title: "CTRL Weight",
  description: "4-cell platform scale live readout",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body
        style={{
          margin: 0,
          fontFamily: "system-ui, -apple-system, sans-serif",
          background: "#0a0a0a",
          color: "#eaeaea",
          minHeight: "100vh",
        }}
      >
        {children}
      </body>
    </html>
  );
}
