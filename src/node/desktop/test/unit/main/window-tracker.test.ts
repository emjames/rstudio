/*
 * window-tracker.test.ts
 *
 * Copyright (C) 2021 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

import { describe } from 'mocha';
import { assert } from 'chai';

import { WindowTracker } from '../../../src/main/window-tracker';
import { DesktopBrowserWindow } from '../../../src/main/desktop-browser-window';
import { GwtWindow } from '../../../src/main/gwt-window';

describe('window-tracker', () => {
  it('empty after creation', () => {
    assert.equal(new WindowTracker().length(), 0);
  });
  it('tracks and returns a window by name', () => {
    const tracker = new WindowTracker();
    const oneWin = new DesktopBrowserWindow(false, 'some name');
    tracker.addWindow('one', oneWin);
    assert.equal(tracker.length(), 1);
    const result = tracker.getWindow('one');
    assert.isObject(result);
    assert.deepEqual(result, oneWin);
  });
  it('tracks and returns two windows by name', () => {
    const tracker = new WindowTracker();
    const oneWin = new DesktopBrowserWindow(false, 'some name');
    tracker.addWindow('one', oneWin);
    const twoWin = new DesktopBrowserWindow(false, 'another name');
    tracker.addWindow('two', twoWin);
    const twoResult = tracker.getWindow('two');
    const oneResult = tracker.getWindow('one');
    assert.isObject(oneResult);
    assert.deepEqual(oneResult, oneWin);
    assert.isObject(twoResult);
    assert.deepEqual(twoResult, twoWin);
  });
  it('duplicate name replaces the original', () => {
    const tracker = new WindowTracker();
    const oneWin = new DesktopBrowserWindow(false, 'some name');
    tracker.addWindow('one', oneWin);
    const twoWin = new GwtWindow(false, 'the gwt window');
    tracker.addWindow('one', twoWin);
    assert.equal(tracker.length(), 1);
    const result = tracker.getWindow('one');
    assert.deepEqual(result, twoWin);
  });
  it('delete window removes it from map', () => {
    const tracker = new WindowTracker();
    const oneWin = new DesktopBrowserWindow(false, 'some name');
    tracker.addWindow('one', oneWin);
    oneWin.window?.close();
    assert.equal(tracker.length(), 0);
  });
});
