import { Button } from '@mui/material'
import React from 'react'
import style from './navigationbutton.module.scss'
import { useNavigate } from 'react-router-dom';
import { ZURUECK } from '../Internationalization/i18n';

type Props = {
    destination: string
}

const NavigationButton = (props: Props) => {
    const navigate = useNavigate();

    return (
        <div className={style.footer}>
            <Button
                className={style.footerButton}
                variant='contained'
                onClick={() => navigate(props.destination)}
                sx={{ backgroundColor: "primary.main" }}
            >
                {ZURUECK}
            </Button>
        </div>
    )
}

export default NavigationButton