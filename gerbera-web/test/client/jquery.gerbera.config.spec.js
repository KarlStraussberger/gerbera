import configMetaJson from './fixtures/config_meta';
import configValueJson from './fixtures/config_values';

describe('The jQuery Configgrid', () => {
  'use strict';
  let dataGrid;

  beforeEach(() => {
    fixture.setBase('test/client/fixtures');
    fixture.load('index.html');
    dataGrid = $('#configgrid');
  });

  afterEach(() => {
    fixture.cleanup();
  });

  it('creates a tree based on config and values', () => {
    dataGrid.config({
      setup: configMetaJson.config,
      values: configValueJson.values,
      meta: configValueJson.meta,
      choice: 'standard',
      chooser: {
        minimal: {caption: 'Minimal', fileName: './fixtures/config_meta.json'}
      },
      addResultItem: null,
      configModeChanged: null,
      itemType: 'config'
    });

    expect(dataGrid.find('ul').length).toBe(65);
    expect(dataGrid.find('li.grb-config').length).toBe(173);
    expect(dataGrid.find('li.grb-config').get(8).innerText).toContain('Model Number');
    expect(dataGrid.find('#grb_line__server_modelNumber').find('input').get(0).value).toContain('2.2.0');
  });
});
