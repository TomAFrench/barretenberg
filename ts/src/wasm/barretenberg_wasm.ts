import isNode from "detect-node";

import {
  AsyncCallState,
  AsyncFnState,
  NodeDataStore,
  WasmModule,
  WebDataStore,
} from "@aztec/wasm-worker";
import { readFile } from "fs/promises";
import { fetch } from "cross-fetch";
import { dirname } from "path";
import { fileURLToPath } from "url";
import { numToUInt32LE } from "./serialize.js";

/**
 * Get the WASM binary for barretenberg.
 * @returns The binary buffer.
 */
export async function fetchCode() {
  if (isNode) {
    const __dirname = dirname(fileURLToPath(import.meta.url));
    return await readFile(__dirname + "/barretenberg.wasm");
  } else {
    const res = await fetch("/barretenberg.wasm");
    return Buffer.from(await res.arrayBuffer());
  }
}

/**
 * A low-level wrapper for an instance of Barretenberg WASM.
 */
export class BarretenbergWasm {
  private store = isNode ? new NodeDataStore() : new WebDataStore();
  private wasm!: WasmModule;
  private asyncCallState = new AsyncCallState();

  /**
   * Create and initialize a BarretenbergWasm module.
   * @param initial - Initial memory pages.
   * @returns The module.
   */
  public static async new(initial?: number) {
    const barretenberg = new BarretenbergWasm();
    await barretenberg.init(initial);
    return barretenberg;
  }
  constructor(private loggerName?: string) {}

  /**
   * 20 pages by default. 20*2**16 \> 1mb stack size plus other overheads.
   * 8192 maximum by default. 512mb.
   * @param initial - Initial memory pages.
   * @param maximum - Max memory pages.
   */
  public async init(initial = 20, maximum = 8192) {
    const { asyncCallState, store } = this;
    let wasm: WasmModule;
    this.wasm = wasm = new WasmModule(
      await fetchCode(),
      (module) => ({
        // These are functions implementations for imports we've defined are needed.
        // The native C++ build defines these in a module called "env". We must implement TypeScript versions here.
        /**
         * Log a string from barretenberg.
         * @param addr - The string address to log.
         */
        logstr(addr: number) {
          const str = wasm.getMemoryAsString(addr);
          const m = wasm.getMemory();
          const str2 = `${str} (mem: ${(m.length / (1024 * 1024)).toFixed(
            2
          )}MB)`;
          wasm.getLogger()(str2);
        },
        /**
         * Read the data associated with the key located at keyAddr.
         * Malloc data within the WASM, copy the data into the WASM, and return the address to the caller.
         * The caller is responsible for taking ownership of (and freeing) the memory at the returned address.
         */
        // eslint-disable-next-line camelcase
        get_data: asyncCallState.wrapImportFn(
          (state: AsyncFnState, keyAddr: number, lengthOutAddr: number) => {
            const key = wasm.getMemoryAsString(keyAddr);
            if (!state.continuation) {
              // We are in the initial code path. Start the async fetch of data, return the promise.
              wasm.getLogger()(`get_data: key: ${key}`);
              return store.get(key);
            } else {
              const data = state.result as Buffer | undefined;
              if (!data) {
                wasm.writeMemory(numToUInt32LE(0), lengthOutAddr);
                wasm.getLogger()(`get_data: no data found for: ${key}`);
                return 0;
              }
              const dataAddr = wasm.call("bbmalloc", data.length);
              wasm.writeMemory(numToUInt32LE(data.length), lengthOutAddr);
              wasm.writeMemory(data, dataAddr);
              wasm.getLogger()(
                `get_data: data at ${dataAddr} is ${data.length} bytes.`
              );
              return dataAddr;
            }
          }
        ),
        // eslint-disable-next-line camelcase
        set_data: asyncCallState.wrapImportFn(
          (
            state: AsyncFnState,
            keyAddr: number,
            dataAddr: number,
            dataLength: number
          ) => {
            if (!state.continuation) {
              const key = wasm.getMemoryAsString(keyAddr);
              wasm.getLogger()(
                `set_data: key: ${key} addr: ${dataAddr} length: ${dataLength}`
              );
              return store.set(
                key,
                Buffer.from(
                  wasm.getMemorySlice(dataAddr, dataAddr + dataLength)
                )
              );
            }
          }
        ),
        memory: module.getRawMemory(),
      }),
      this.loggerName
    );
    await wasm.init(initial, maximum);
    this.asyncCallState.init(wasm);
  }

  /**
   * Get a slice of memory between two addresses.
   * @param start - The start address.
   * @param end - The end address.
   * @returns A Uint8Array view of memory.
   */
  public getMemorySlice(start: number, end: number) {
    return this.wasm.getMemorySlice(start, end);
  }

  /**
   * Write data into the heap.
   * @param arr - The data to write.
   * @param offset - The address to write data at.
   */
  public writeMemory(arr: Uint8Array, offset: number) {
    this.wasm.writeMemory(arr, offset);
  }

  /**
   * Calls into the WebAssembly.
   * @param name - The method name.
   * @param args - The arguments to the method.
   * @returns The numeric integer or address result.
   */
  public call(name: string, ...args: any): number {
    return this.wasm.call(name, ...args);
  }

  /**
   * Uses asyncify to enable async callbacks into js.
   * @see https://kripken.github.io/blog/wasm/2019/07/16/asyncify.html
   * @param name - The method name.
   * @param args - The arguments to the method.
   * @returns The numeric integer or address result.
   */
  public async asyncCall(name: string, ...args: any): Promise<number> {
    return await this.asyncCallState.call(name, ...args);
  }
}
