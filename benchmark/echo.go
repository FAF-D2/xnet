package main

import (
    "net"
)

func handleConn(c net.Conn) {
    buf := make([]byte, 256)

    for {
        n, err := c.Read(buf)
        if err != nil {
            return
        }

        _, err = c.Write(buf[:n])
        if err != nil {
            return
        }
    }
}

func main() {
    ln, err := net.Listen("tcp", ":52333")
    if err != nil {
        panic(err)
    }

    for {
        conn, err := ln.Accept()
        if err != nil {
            continue
        }
        go handleConn(conn)
    }
}
