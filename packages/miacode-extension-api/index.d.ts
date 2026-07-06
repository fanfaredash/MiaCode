export interface ExtensionContext {
  extensionPath: string;
  subscriptions: Array<{ dispose(): void }>;
  log(message: string): Promise<unknown>;
}

export interface ActiveDocumentSnapshot {
  uri: string;
  languageId: "simai" | string;
  text: string;
  activeDifficultyId: number;
  dirty: boolean;
}

export interface Disposable {
  dispose(): void;
}

export interface MiaCodeApi {
  commands: {
    registerCommand(command: string, callback: () => void | Promise<void>): Disposable;
  };
  window: {
    showInformationMessage(message: string): Promise<unknown>;
    showWarningMessage(message: string): Promise<unknown>;
    showErrorMessage(message: string): Promise<unknown>;
    openPreferences(): Promise<{ ok: boolean; error?: string }>;
  };
  workspace: {
    getActiveDocument(): Promise<ActiveDocumentSnapshot>;
    applyDocumentEdit(edit: { text: string }): Promise<{ ok: boolean; error?: string }>;
  };
  diagnostics: {
    validateDocument(): Promise<{ ok: boolean }>;
  };
}

export function activate(context: ExtensionContext): void | Promise<void>;
export function deactivate(): void | Promise<void>;

declare global {
  const miacode: MiaCodeApi;
}
