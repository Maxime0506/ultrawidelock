import { render } from "@opentui/solid"
import { App } from "./app"

render(() => <App />, {
  exitOnCtrlC: true,
  screenMode: "alternate-screen",
  clearOnShutdown: true,
  onDestroy: () => process.exit(0)
})
