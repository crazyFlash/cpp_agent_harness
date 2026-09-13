#!/usr/bin/env python3

import http.server
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading


class FakeResponsesHandler(http.server.BaseHTTPRequestHandler):
    request_error = None

    def do_POST(self):
        try:
            if self.path != "/v1/responses":
                raise AssertionError(f"unexpected path: {self.path}")
            if self.headers.get("Authorization") != "Bearer integration-secret":
                raise AssertionError("missing or incorrect bearer token")

            length = int(self.headers["Content-Length"])
            request = json.loads(self.rfile.read(length))
            if request["model"] != "integration-model":
                raise AssertionError("model was not loaded from configuration")
            if request["input"][-1]["content"] != "hello from integration test":
                raise AssertionError("user input was not encoded")

            response = {
                "id": "resp_integration",
                "status": "completed",
                "output": [
                    {
                        "type": "message",
                        "content": [
                            {"type": "output_text", "text": "integration response"}
                        ],
                    }
                ],
            }
            encoded = json.dumps(response).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
        except Exception as error:  # surfaced in the parent thread below
            type(self).request_error = error
            self.send_response(500)
            self.end_headers()

    def log_message(self, *_args):
        pass


def main():
    repository = pathlib.Path(__file__).resolve().parents[1]
    executable = (
        pathlib.Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else repository / "cpp-agent"
    )
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeResponsesHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    try:
        with tempfile.TemporaryDirectory(prefix="cpp-agent-config-") as directory:
            config_path = pathlib.Path(directory) / "agent.json"
            config_path.write_text(
                json.dumps(
                    {
                        "provider": "responses_api",
                        "api": {
                            "base_url": f"http://127.0.0.1:{server.server_port}/v1",
                            "model": "integration-model",
                            "api_key_env": "INTEGRATION_API_KEY",
                            "require_api_key": True,
                            "timeout_ms": 5000,
                            "store": False,
                        },
                    }
                )
            )
            environment = os.environ.copy()
            environment["INTEGRATION_API_KEY"] = "integration-secret"
            process = subprocess.run(
                [str(executable), "--config", str(config_path)],
                input="hello from integration test\nquit\n",
                text=True,
                capture_output=True,
                cwd=repository,
                env=environment,
                timeout=10,
                check=False,
            )
            if process.returncode != 0:
                raise AssertionError(
                    f"CLI exited {process.returncode}: {process.stderr}"
                )
            if "provider=responses_api" not in process.stdout:
                raise AssertionError("CLI did not select the configured provider")
            if "assistant: integration response" not in process.stdout:
                raise AssertionError(
                    f"CLI did not print the API response: {process.stdout}"
                )
            if FakeResponsesHandler.request_error is not None:
                raise FakeResponsesHandler.request_error
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)

    print("API integration test passed")


if __name__ == "__main__":
    main()
