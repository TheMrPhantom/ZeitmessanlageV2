import React, { useEffect, useState } from 'react';
import { ThemeProvider } from '@mui/material/styles';
import Cookies from 'js-cookie';
import { themes } from './Components/Common/Theme';

import allReducer from './Reducer/reducerCombiner';
import { createStore } from 'redux';
import { composeWithDevTools } from 'redux-devtools-extension';
import { Provider } from 'react-redux';
import { Box, CssBaseline, ScopedCssBaseline } from '@mui/material';
import { BrowserRouter as Router } from 'react-router-dom';
import TopBar from './Components/Common/TopBar/TopBar';
import Routing from './Components/Routing/Routing';
import LoginChecker from './Components/Common/LoginChecker/LoginChecker';
import Toast from './Components/Common/Toast/Toast';

declare global {
  interface Window {
    globalTS: {
      DOMAIN: string,
      MOBILE_THRESHOLD: number,
      ICON_COLOR: string,
      ORGANISATION_NAME: string,
      ABOUT_LINK: string,
      PRIVACY_LINK: string,
      ADDITIONAL_INFORMATION: string,
      WELCOME_TEXT_0: string,
      WELCOME_TEXT_0_ADMIN: string,
      WELCOME_TEXT_1: string,
      HOME_BUTTON: string,
      TRANSACTION_LIMIT: number,
      OIDC_BUTTON_TEXT: null | string,
      AUTH_COOKIE_PREFIX: string
    };
  }
}

function App() {
  const [themeCookie, setthemeCookie] = useState(0)
  const store = createStore(allReducer, composeWithDevTools())

  useEffect(() => {
    setthemeCookie(Cookies.get("theme") !== undefined ? Number(Cookies.get("theme")) : 3)
  }, [])

  return (
    <ThemeProvider theme={themes[themeCookie]}>
      <Router>
        <ScopedCssBaseline>
          <div className="App">
            <CssBaseline />
            <Provider store={store}>
              <LoginChecker />
              <Toast />
              <Box sx={{ display: 'flex' }}>
                <TopBar />
                <Box component="main" sx={{ flexGrow: 1, p: 3, padding: 0 }}>
                  <Routing />
                </Box>
              </Box>
            </Provider>
          </div>
        </ScopedCssBaseline>
      </Router>
    </ThemeProvider >
  );
}

export default App;
